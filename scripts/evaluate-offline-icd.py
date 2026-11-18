"""Reproducible CPU-only comparison on a fixed synthetic ICD evaluation.

Run with the local pinned llama-server and three hash-verified GGUF files.
This deliberately isolates synthesis quality from retrieval quality: every
model gets the same relevant excerpts and a conflicting, clearly scoped row.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import urllib.request
from ctypes import wintypes as w


class Basic(ctypes.Structure):
    _fields_ = [("process", ctypes.c_int64), ("job", ctypes.c_int64), ("flags", w.DWORD),
                ("minws", ctypes.c_size_t), ("maxws", ctypes.c_size_t), ("active", w.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD)]


class Limits(ctypes.Structure):
    _fields_ = [("basic", Basic), ("io", ctypes.c_uint64 * 6), ("process_memory", ctypes.c_size_t),
                ("job_memory", ctypes.c_size_t), ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]


class Cpu(ctypes.Structure):
    _fields_ = [("flags", w.DWORD), ("rate", w.DWORD)]


def cases():
    routes = [
        ("Hydraulics", "Pressure", "bar", "unsigned", "12", "MSW", "active", "bypass"),
        ("Navigation", "Heading", "deg", "float32", "27", "LSW", "valid", "standby"),
        ("Electrical", "Current", "ampere", "signed", "16", "MSW", "trip", "isolated"),
        ("Propulsion", "Speed", "rpm", "BCD", "8", "LSW", "ready", "warmup"),
        ("Environmental", "Temperature", "kelvin", "float64", "4", "MSW", "fault", "purge"),
    ]
    for i, (group, signal, unit, encoding, word, order, flag, condition) in enumerate(routes):
        evidence = (f"[1] ICD page {i+1}, {group} message H{i+1}: {signal} uses {unit}, "
                    f"{encoding}, starting at word {word}, {order} first. Flag F means {flag}.\n"
                    f"[2] ICD page {i+6}, {group}: plot review is enabled in {condition} mode. "
                    f"The calibration offset is unspecified.\n"
                    f"[3] ICD page {i+11}, Bench simulator only: {signal} uses volts, float16, "
                    f"word 31, flag F means disabled. These rules do not apply to {group}.")
        for question, answer, citation in [
            (f"What unit does {signal} use?", unit, 1),
            (f"Which encoding applies to {signal}?", encoding, 1),
            (f"What is the starting word for {signal}?", word, 1),
            (f"Which word comes first for {signal}?", order, 1),
            ("What does flag F mean?", flag, 1),
            ("Which mode enables plot review?", condition, 2),
            ("What is the calibration offset?", "unspecified", 2),
            ("Does the bench simulator rule apply?", "no", 3),
        ]:
            yield dict(question=f"For {group}: {question}", expected=answer, citation=citation, evidence=evidence)


def evaluate(server, model, model_hash, report):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateJobObjectW.restype = w.HANDLE
    k32.OpenProcess.restype = w.HANDLE
    k32.SetInformationJobObject.argtypes = (w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD)
    k32.QueryInformationJobObject.argtypes = (w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.c_void_p)
    k32.AssignProcessToJobObject.argtypes = (w.HANDLE, w.HANDLE)
    k32.CloseHandle.argtypes = (w.HANDLE,)
    with model.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != model_hash:
            raise ValueError(f"GGUF identity mismatch: {model.name}")
    job = k32.CreateJobObjectW(None, None)
    limits = Limits()
    limits.basic.flags = 0x2000 | 0x100
    limits.process_memory = 2560 * 1024**2
    cpu = Cpu(5, 2000)
    process = None
    try:
        if not job or not k32.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not k32.SetInformationJobObject(job, 15, ctypes.byref(cpu), ctypes.sizeof(cpu)):
            raise ctypes.WinError(ctypes.get_last_error())
        with tempfile.TemporaryDirectory(prefix="aerotrace-icd-eval-") as directory:
            key = os.urandom(24).hex()
            keyfile = Path(directory) / "key"
            keyfile.write_text(key + "\n", encoding="ascii")
            with socket.socket() as reserved:
                reserved.bind(("127.0.0.1", 0))
                port = reserved.getsockname()[1]
            url = f"http://127.0.0.1:{port}"
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

            def call(path, body=None):
                request = urllib.request.Request(url + path,
                    data=None if body is None else json.dumps(body).encode(),
                    headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
                with opener.open(request, timeout=90) as response:
                    return json.load(response)

            started = time.perf_counter()
            with report.with_suffix(".server.log").open("w", encoding="utf8") as log:
                process = subprocess.Popen([str(server), "--model", str(model), "--host", "127.0.0.1",
                    "--port", str(port), "--api-key-file", str(keyfile), "--ctx-size", "2048", "--threads", "2",
                    "--threads-batch", "2", "--threads-http", "1", "--parallel", "1", "--n-gpu-layers", "0",
                    "--offline", "--no-webui", "--no-warmup"], cwd=server.parent,
                    stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW | 4)
                if not k32.AssignProcessToJobObject(job, w.HANDLE(int(process._handle))):
                    raise ctypes.WinError(ctypes.get_last_error())
                resume = ctypes.WinDLL("ntdll").NtResumeProcess
                resume.argtypes = (w.HANDLE,)
                if resume(w.HANDLE(int(process._handle))) != 0:
                    raise RuntimeError("Could not resume isolated inference")
                while True:
                    if process.poll() is not None or time.perf_counter() - started > 90:
                        raise RuntimeError("Local model failed to become ready")
                    try:
                        call("/health")
                        break
                    except OSError:
                        time.sleep(.3)
                startup = time.perf_counter() - started
                scores = []
                for case in cases():
                    now = time.perf_counter()
                    response = call("/v1/chat/completions", dict(
                        messages=[dict(role="system", content="Use only the ICD excerpts. Answer with one value or word and its citation, for example: amber [2]."),
                                  dict(role="user", content=case["evidence"] + "\n\n" + case["question"])],
                        temperature=0, seed=370, max_tokens=20, chat_template_kwargs={"enable_thinking": False}))
                    answer = response["choices"][0]["message"].get("content") or ""
                    elapsed = time.perf_counter() - now
                    citations = [int(x) for x in re.findall(r"\[(\d+)\]", answer)]
                    normalized = re.sub(r"\[\d+\]|[^\w\s]", "", re.sub(r"\[\d+\]", "", answer)).strip().casefold()
                    correct = normalized == case["expected"].casefold() and citations == [case["citation"]]
                    scores.append(dict(case, answer=answer, correct=correct, seconds=elapsed))
                    print(f"{model.name}: {len(scores)}/40 · {elapsed:.2f}s · {'pass' if correct else 'miss'}", flush=True)
                    report.write_text(json.dumps(dict(model=model.name, cases=scores), indent=2), encoding="utf8")
                k32.QueryInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits), None)
                ordered = sorted(s["seconds"] for s in scores[1:])
                # Strict scoring: any answer other than the expected fact/citation
                # is unsupported until reviewed, rather than counted as safe.
                unsupported = sum(not s["correct"] and bool(s["answer"].strip()) for s in scores)
                result = dict(model=model.name, sha256=model_hash, startup_seconds=startup,
                              correct=sum(s["correct"] for s in scores), unsupported_or_wrong=unsupported,
                              warm_p95_seconds=ordered[math.ceil(len(ordered)*.95)-1],
                              peak_process_commit_bytes=limits.peak_process, cases=scores)
                report.write_text(json.dumps(result, indent=2), encoding="utf8")
                return result
    finally:
        if job:
            k32.CloseHandle(job)
        if process:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("models", nargs="+", help="Absolute or relative model path followed by =SHA256")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for item in args.models:
        path, digest = item.rsplit("=", 1)
        model = Path(path).resolve()
        try:
            results.append(evaluate(args.server.resolve(), model, digest, args.output / (model.stem + ".json")))
        except Exception as error:
            results.append(dict(model=model.name, error=str(error)))
            print(f"{model.name}: {error}", flush=True)
    baseline = results[0]
    candidates = [r for r in results[1:] if "error" not in r and "error" not in baseline and
                  r["correct"] >= baseline["correct"] + 4 and r["unsupported_or_wrong"] == 0 and
                  r["peak_process_commit_bytes"] > 0 and r["warm_p95_seconds"] <= 1.25 * baseline["warm_p95_seconds"]]
    winner = max(candidates, key=lambda r: (r["correct"], -r["warm_p95_seconds"]), default=baseline)
    (args.output / "decision.json").write_text(json.dumps(dict(
        fixture="Five synthetic ICD routes; eight identical prompts per route", limits="2 threads; 20% CPU; 2.5 GiB process commit",
        winner=winner["model"], results=[{k:v for k,v in r.items() if k!="cases"} for r in results]), indent=2), encoding="utf8")


if __name__ == "__main__":
    main()
