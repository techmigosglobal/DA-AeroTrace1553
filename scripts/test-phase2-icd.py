"""Exercise the packed document worker with the Phase Two synthetic ICDs."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("pdfs", type=Path)
    parser.add_argument("report", type=Path)
    options = parser.parse_args()
    assets = options.pdfs.resolve()
    python = options.package.resolve() / "document-runtime" / "python.exe"
    worker = options.package.resolve() / "document-worker" / "worker.py"
    receipts = []

    with tempfile.TemporaryDirectory(prefix="aerotrace-thermal-") as store:
        def run(op, **fields):
            payload = json.dumps(dict(op=op, store=store, **fields)) + "\n"
            reply = subprocess.run([python, "-B", str(worker)], input=payload, capture_output=True,
                                   text=True, encoding="utf-8", timeout=120, check=True)
            events = [json.loads(line) for line in reply.stdout.splitlines()]
            return next(row for row in reversed(events) if row["event"] == "done"), events

        def ask(label, digest, question, required=(), generation=False):
            answer, _ = run("ask", document_hash=digest, question=question,
                            prepare_generation=generation, fast_only=not generation)
            citations = answer.get("citations", [])
            evidence = answer.get("answer", "").casefold()
            if generation:
                evidence += " " + " ".join(item.get("excerpt", "") for item in citations).casefold()
            passed = (all(value.casefold() in evidence for value in required)
                      and any(item.get("hash") == digest and item.get("page") == 2 for item in citations))
            if generation:
                passed = passed and answer.get("needs_generation") is True
            if label == "scanned evidence":
                passed = passed and any(item.get("source_method") == "ocr" for item in citations)
            receipts.append(dict(case=label, passed=passed, mode=answer.get("mode"), citations=citations,
                                 answer=answer.get("answer"), prepared_generation=answer.get("needs_generation", False)))
            return answer

        digests = {}
        for revision in ("B", "A", "B-Scan"):
            result, events = run("ingest", pdf=str(assets / f"Thermal-Bench-ICD-Rev-{revision}.pdf"))
            digests[revision] = result["document_hash"]
            ocr = any(item.get("method") == "ocr" for item in events)
            receipts.append(dict(case="index-"+revision, passed=bool(result["document_hash"]) and
                                 (ocr if revision.endswith("Scan") else not ocr), ocr=ocr))
        b, a, scan = (digests[key] for key in ("B", "A", "B-Scan"))
        ask("temperature", b, "How is Temperature converted? Give the scale and offset.", ("0.5", "-10"))
        ask("follow-up", b, "What about its offset?", ("-10",))
        ask("linear raw", b, "For Temperature, what does raw 0064 convert to?", ("0064", "40"))
        ask("signed", b, "What does FFFE mean for Signed correction?", ("fffe", "-2"))
        ask("multiword", b, "Reference pressure: what do raw words 4148 0000 mean and what is their order?",
            ("12.5", "most significant"))
        ask("nonlinear", b, "How does the nonlinear index convert 000A and 0014?", ("0.01", "1.0", "4.0"))
        ask("revision isolation", a, "What is the Temperature example 0064?", ("30",))
        ask("scanned evidence", scan, "What is the Temperature conversion and raw 0064 example?", ("0.5", "40"))
        ask("prepare scoped generation", b, "Explain how the nonlinear index is defined.", ("nonlinear", "0.01"), True)
        absent, _ = run("ask", document_hash=b, question="Quasargate Unobtainium")
        receipts.append(dict(case="unsupported", passed=not absent["citations"] and
                             "no supporting passage" in absent["answer"].lower(), answer=absent["answer"]))

    options.report.parent.mkdir(parents=True, exist_ok=True)
    options.report.write_text(json.dumps(receipts, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for case in receipts:
        print(("PASS" if case["passed"] else "FAIL") + " " + case["case"])
    return 0 if all(case["passed"] for case in receipts) else 1


if __name__ == "__main__":
    raise SystemExit(main())
