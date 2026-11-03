"""Executable offline tests with real digital and image-only PDF fixtures."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import worker
from review_actions import propose


class ReviewedActionTests(unittest.TestCase):
    def setUp(self):
        self.profile = {"schema": worker.SCHEMA, "signals": [dict(id="TEMP", unit="degC", rt=3, sa=2, word=0, lsb=0, width=16, scale=.1, offset=0, signed=False, transmit=False, numeric_type="integer", source_hash="a"*64)]}

    def test_review_is_non_mutating_and_scoped(self):
        result = propose("Set TEMP scale to 0.2", self.profile, "a"*64)
        self.assertEqual(self.profile["signals"][0]["scale"], .1)
        self.assertEqual(result["profile"]["signals"][0]["scale"], .2)
        self.assertIn("Operator clarification", result["summary"])
        self.assertEqual(propose("Plot TEMP", self.profile, "a"*64)["kind"], "plot")
        with self.assertRaises(ValueError):
            propose("Set TEMP scale to 0.2", self.profile, "b"*64)

    def test_reject_ambiguous_or_partial_instructions(self):
        for text in ["set TEMP scale to 0.2 and delete records", "Set TEMP word index to 32", "Set TEMP word index to 1.5", "Set TEMP scale to NaN", "Set UNKNOWN scale to 1", "run a shell", "TEMP uses the second word"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                propose(text, self.profile, "a"*64)
        self.assertEqual(propose("TEMP offset is -40", self.profile, "a"*64)["profile"]["signals"][0]["offset"], -40)
        self.assertTrue(propose("TEMP is signed", self.profile, "a"*64)["profile"]["signals"][0]["signed"])
        self.assertEqual(propose("Set TEMP bus to B", self.profile, "a"*64)["profile"]["signals"][0]["bus"], 1)
        self.assertEqual(propose("Set TEMP unit to kelvin", self.profile, "a"*64)["profile"]["signals"][0]["unit"], "kelvin")
        with self.assertRaises(ValueError):
            propose("Set TEMP type to float64", self.profile, "a"*64)


def fixture_pdf(path, temperature_unit="degC"):
    headers = ["Name", "RT", "SA", "Direction", "Word index (0-based)", "LSB", "Width", "Scale", "Offset", "Unit", "Type", "Word order"]
    rows = [headers, ["TEMP", "3", "2", "Rx", "0", "0", "16", "0.01", "0", temperature_unit, "uint16", "MSW first"],
            ["PRESSURE", "3", "2", "Rx", "1", "0", "16", "0.1", "5", "kPa", "uint16", "MSW first"],
            ["COUNTER", "3", "2", "Rx", "2", "0", "32", "1", "0", "count", "uint32", "MSW first"]]
    widths = [105, 28, 28, 65, 140, 30, 45, 55, 50, 42, 65, 75]
    x_positions = [30]
    for width in widths:
        x_positions.append(x_positions[-1] + width)
    stream = ["0.4 w", "BT /F1 14 Tf 30 445 Td (ICD engineering conversion acceptance fixture) Tj ET",
              f"BT /F1 10 Tf 30 420 Td (Temperature TEMP is measured in {temperature_unit}. Word indices are zero-based. LSB = 0.) Tj ET"]
    for x in x_positions:
        stream.append(f"{x} 245 m {x} 385 l S")
    for row in range(len(rows) + 1):
        y = 385 - row * 35
        stream.append(f"30 {y} m {x_positions[-1]} {y} l S")
    for index, row in enumerate(rows):
        for column, text in enumerate(row):
            escaped = text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
            stream.append(f"BT /F1 9 Tf {x_positions[column]+4} {365-index*35} Td ({escaped}) Tj ET")
    content = "\n".join(stream).encode("ascii")
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
               b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 960 500] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
               b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>", f"<< /Length {len(content)} >>\nstream\n".encode() + content + b"\nendstream"]
    output = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for index, obj in enumerate(objects, 1):
        offsets.append(len(output))
        output += f"{index} 0 obj\n".encode() + obj + b"\nendobj\n"
    start = len(output)
    output += f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode()
    for offset in offsets[1:]:
        output += f"{offset:010} 00000 n \n".encode()
    output += f"trailer << /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n".encode()
    path.write_bytes(output)


class Documents(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def ingest(self, path):
        with contextlib.redirect_stdout(io.StringIO()):
            return worker.ingest({"pdf": str(path), "store": str(self.root / "store")})

    def test_digital_pdf_mapping_retrieval_and_idempotence(self):
        path = self.root / "reference.pdf"
        fixture_pdf(path)
        result = self.ingest(path)
        profile = result["profile"]
        self.assertTrue(profile["automatic"], profile)
        self.assertEqual([item["id"] for item in profile["signals"]], ["TEMP", "PRESSURE", "COUNTER"])
        self.assertEqual(profile["signals"][1]["offset"], 5)
        self.assertEqual(profile["signals"][2]["width"], 32)
        self.assertEqual(profile["signals"][0]["source_page"], 1)
        self.ingest(path)
        answer = worker.ask({"store": str(self.root / "store"), "question": "Temperature TEMP unit"})
        self.assertTrue(answer["citations"])
        self.assertIn("degC", answer["answer"])
        self.assertEqual(answer["mode"], "extractive")
        self.assertEqual(answer["citations"][0]["hash"], profile["sha256"])
        missing = worker.ask({"store": str(self.root / "store"), "question": "unfindable_xyz"})
        self.assertFalse(missing["citations"])

    def test_selected_icd_chat_isolation_followup_and_persistence(self):
        a,b=self.root/"external.pdf",self.root/"internal.pdf"
        fixture_pdf(a);fixture_pdf(b,"Kelv")
        first=self.ingest(a)["profile"];second=self.ingest(b)["profile"]
        store=str(self.root/"store")
        def ask(digest,question):
            with contextlib.redirect_stdout(io.StringIO()):
                return worker.ask({"store":store,"document_hash":digest,"question":question})
        answer=ask(first["sha256"],"What is the TEMP unit?")
        self.assertIn("degC",answer["answer"]);self.assertNotIn("Kelv",answer["answer"])
        self.assertTrue(all(c["hash"]==first["sha256"] for c in answer["citations"]))
        followup=ask(first["sha256"],"What is its scale?")
        self.assertIn("scale 0.01",followup["answer"]);self.assertEqual(len(followup["history"]),4)
        independent=ask(second["sha256"],"What is the TEMP unit?")
        self.assertIn("Kelv",independent["answer"]);self.assertEqual(len(independent["history"]),2)
        evidence=ask(first["sha256"],"Temperature overview evidence")
        self.assertTrue(evidence["citations"]);self.assertTrue(all(c["hash"]==first["sha256"] for c in evidence["citations"]))
        restored=worker.library({"store":store,"document_hash":first["sha256"]})
        self.assertEqual(len(restored["documents"]),2);self.assertEqual(len(restored["history"]),6)
        self.assertEqual(restored["profile"]["signals"][0]["scale"],.01)
        with self.assertRaises(ValueError):
            ask("0"*64,"TEMP unit")

    def test_scanned_pdf_uses_local_ocr(self):
        import pypdfium2 as pdfium
        native = self.root / "native.pdf"
        scan = self.root / "scan.pdf"
        fixture_pdf(native)
        with pdfium.PdfDocument(native) as pdf:
            page = pdf[0]
            bitmap = page.render(scale=3)
            bitmap.to_pil().convert("RGB").save(scan, "PDF", resolution=216)
            bitmap.close()
            page.close()
        result = self.ingest(scan)
        # OCR may require review, but evidence must be searchable and never silently
        # converted into a different addressing or scale rule.
        db = worker.database(self.root / "store")
        text = db.execute("SELECT text FROM chunks").fetchone()[0]
        db.close()
        self.assertIn("TEMP", text)
        self.assertIn("PRESSURE", text)
        for mapping in result["profile"]["signals"]:
            self.assertEqual(mapping["rt"], 3)
            self.assertEqual(mapping["sa"], 2)
            self.assertEqual(mapping["evidence"]["method"], "ocr")
            self.assertEqual(mapping["source_document"], scan.name)
        for pending in result["profile"]["review"]:
            if "candidate" in pending:
                self.assertEqual(pending["candidate"]["source_document"], scan.name)

    def test_ambiguity_and_expressions(self):
        headers = ["Name", "RT", "SA", "Direction", "Word", "LSB", "Width", "Scale", "Offset", "Unit", "Type"]
        row = ["TEMP", "3", "2", "Rx", "0", "0", "16", "0.1", "0", "degC", "uint16"]
        mapped, pending = worker.parse_table([headers, row], 1, "digital", "hash", "")
        self.assertFalse(mapped)
        self.assertIn("indexing", pending[0]["reason"])
        self.assertEqual(worker.number("2^-10"), 1/1024)
        with self.assertRaises(ValueError):
            worker.number("__import__('os').system('bad')")
        headers[4] = "Word index (0-based)"
        mapped, pending = worker.parse_table([headers, row], 1, "ocr", "hash", "", .90)
        self.assertFalse(mapped)
        self.assertIn("confidence", pending[0]["reason"])


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3 and sys.argv[1] == "--write-fixture":
        destination = Path(sys.argv[2]); destination.parent.mkdir(parents=True, exist_ok=True)
        fixture_pdf(destination)
    else:
        unittest.main(verbosity=2)
