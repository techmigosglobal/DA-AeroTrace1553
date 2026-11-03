"""Verify actual supplied PDF input, mapping parity and cited retrieval offline."""
import contextlib
import io
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/"tools/document_worker"))
import worker

kit = root/"examples/demo-icd"
store = root/"out/validation/demo-icd-store"
with contextlib.redirect_stdout(io.StringIO()):
    result = worker.ingest({"pdf": str(kit/"AeroTrace-Demo-ICD.pdf"), "store": str(store)})
expected = json.loads((kit/"AeroTrace-Demo-ICD.json").read_text(encoding="utf-8"))
actual = result["profile"]
assert actual["automatic"], actual
assert len(actual["signals"]) == 9, actual
for left, right in zip(expected["signals"], actual["signals"]):
    for key in ("id", "rt", "sa", "transmit", "word", "lsb", "width", "signed", "scale", "offset", "unit", "numeric_type", "lsw_first"):
        assert left[key] == right[key], (key, left, right)
with contextlib.redirect_stdout(io.StringIO()):
    answer = worker.ask({"store": str(store), "question": "TEMP temperature engineering unit"})
assert answer["citations"] and "degC" in answer["answer"], answer
report = dict(passed=True, signals=9, automatic=True, cited_answer=answer["answer"], citations=answer["citations"])
if "--scan" in sys.argv:
    progress = io.StringIO()
    with contextlib.redirect_stdout(progress):
        scanned = worker.ingest({"pdf": str(kit/"AeroTrace-Demo-ICD-scanned.pdf"), "store": str(store)})["profile"]
    assert any(json.loads(line).get("method") == "ocr" for line in progress.getvalue().splitlines()), "OCR was not exercised"
    expected_by_id = {signal["id"]: signal for signal in expected["signals"]}
    for signal in scanned["signals"]:
        reference = expected_by_id[signal["id"]]
        for key in ("rt", "sa", "word", "lsb", "width", "scale", "offset", "signed", "unit", "transmit"):
            assert signal[key] == reference[key], ("OCR field mismatch", key, signal)
    assert scanned["signals"] or scanned["review"], "OCR returned no mapping evidence"
    if scanned["automatic"]:
        assert len(scanned["signals"]) == 9, "OCR must not silently omit demo rules"
    report["scan"] = dict(method="ocr", automatic=scanned["automatic"], validated=len(scanned["signals"]), review=len(scanned["review"]))
(root/"out/validation/demo-icd-verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({key: value for key, value in report.items() if key not in ("cited_answer", "citations")}))
