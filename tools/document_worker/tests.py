"""Executable offline tests with real digital and image-only PDF fixtures."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

# The embedded Windows Python can prefer the installed worker via python._pth.
# Use the sibling source modules when this suite is run from a checkout.
sys.path.insert(0, str(Path(sys.modules["worker"].__file__).resolve().parent if "worker" in sys.modules else Path(__file__).resolve().parent))
import worker
from review_actions import propose


class GenerationProtocolTests(unittest.TestCase):
    def test_streaming_citations_and_recoverable_generation_failure(self):
        import http.server
        import threading
        class Handler(http.server.BaseHTTPRequestHandler):
            answer = ["<thi", "nk>hidden analysis</thi", "nk>RT 4 transmits 32 words [1]."]
            def do_POST(self):
                if self.headers.get("Authorization") != "Bearer test-key":
                    self.send_error(401)
                    return
                body=json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                if self.path=="/tokenize":
                    data=json.dumps({"tokens":[0]*(len(body.get("content",""))//4)}).encode()
                    self.send_response(200);self.send_header("Content-Type","application/json");self.send_header("Content-Length",str(len(data)));self.end_headers();self.wfile.write(data);return
                self.server.request=body
                self.send_response(200)
                self.send_header("Content-Type","text/event-stream")
                self.end_headers()
                for chunk in self.answer:
                    self.wfile.write(("data: "+json.dumps({"choices":[{"delta":{"content":chunk}}]})+"\n\n").encode())
                self.wfile.write(b"data: [DONE]\n\n")
            def log_message(self,*args):
                pass
        server=http.server.HTTPServer(("127.0.0.1",0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory() as store:
                prepared={"document_hash":"a"*64,"answer":"[1] Source evidence.","mode":"extractive","citations":[{"document":"fixture.pdf","page":1,"hash":"a"*64,"excerpt":"RT 4 SA 2 transmits 32 words."}]}
                request={"store":store,"op":"explain","question":"Explain the message route","prepared":prepared,"local_model_url":f"http://127.0.0.1:{server.server_port}","model_token":"test-key"}
                events=[]
                old=worker.emit
                worker.emit=lambda event,**data:events.append((event,data))
                try:
                    answer=worker.complete_generation(request)
                    self.assertEqual(answer["mode"],"local-model")
                    self.assertEqual(Path(sys.modules["local_generation"].__file__).parent,Path(worker.__file__).parent)
                    self.assertNotIn("hidden",answer["answer"])
                    self.assertNotIn("think",answer["answer"])
                    self.assertTrue(any(event=="answer_delta" for event,_ in events))
                    self.assertFalse(server.request["chat_template_kwargs"]["enable_thinking"])
                    Handler.answer=["Unsupported uncited explanation."]
                    fallback=worker.complete_generation(request)
                    self.assertEqual(fallback["mode"],"extractive")
                    self.assertIn("valid source citations",fallback["generation_note"])
                    self.assertIn("Source evidence",fallback["answer"])
                    Handler.answer=["RT 4 transmits 32 words [4]."]
                    request["prepared"]=dict(prepared,citations=prepared["citations"]*4)
                    self.assertEqual(worker.complete_generation(request)["mode"],"extractive")
                    request["prepared"]=prepared
                    Handler.answer=["RT 4 transmits 99 words [1]."]
                    unsupported=worker.complete_generation(request)
                    self.assertEqual(unsupported["mode"],"extractive")
                    self.assertIn("number absent",unsupported["generation_note"])
                finally:
                    worker.emit=old
        finally:
            server.shutdown();server.server_close();thread.join()

    def test_complete_tables_survive_conversation_storage(self):
        with tempfile.TemporaryDirectory() as store:
            result={"answer":"39 matching float fields", "mode":"structured-table","query":{"kind":"float","subsystem":""},"table":{"rows":[{"signal":f"ID_{n}","words":f"{n*2}–{n*2+1}"} for n in range(39)]},"citations":[]}
            saved=worker.remember_answer({"store":store},"f"*64,"Which words are float?",result)
            self.assertEqual(len(saved["history"][-1]["result"]["table"]["rows"]),39)
            db=worker.database(store)
            try:
                self.assertEqual(worker.chat_history(db,"f"*64)[-1]["result"]["query"]["kind"],"float")
            finally:
                db.close()

class ReviewedActionTests(unittest.TestCase):
    def setUp(self):
        self.profile = {"schema": worker.SCHEMA, "signals": [dict(id="TEMP", unit="degC", rt=3, sa=2, word=0, lsb=0, width=16, scale=.1, offset=0, signed=False, transmit=False, numeric_type="integer", source_hash="a"*64)]}

    def test_review_is_non_mutating_and_scoped(self):
        result = propose("Set TEMP scale to 0.2", self.profile, "a"*64)
        self.assertEqual(self.profile["signals"][0]["scale"], .1)
        self.assertEqual(result["profile"]["signals"][0]["scale"], .2)
        self.assertIn("Operator instruction", result["summary"])
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


class MessageLayoutTests(unittest.TestCase):
    def test_complete_structured_float_listing_and_scope(self):
        from structured_answers import index_rows, query
        profile={"review": [], "signals": [], "document": "fixture.pdf"}
        for group in range(1,4):
            message=[["MESSAGE ID","04102","RT ADDRESS","04"],
                     ["SUBADDR",str(3 if group==2 else 2),"MSG TYPE","TRANSMIT"],
                     ["WORD COUNT","32","",""]]
            table=[["Word No.","Signal ID","Unit","Word Format","Data type"]]
            for index in range(13):
                table.append([f"{index*2}-{index*2+1}",f"ID_{(group-1)*19+index+1}","Rad","2words","Float"])
            rows=worker.parse_message_layout([message,table],group,"digital","a"*64,{},[f"Subsystem-{group}"])
            for row in rows:
                row["source_document"]="fixture.pdf"
            profile["review"].extend(rows)
        with tempfile.TemporaryDirectory() as store:
            db=worker.database(store)
            index_rows(db,"fixture",profile)
            request={"question":"Which words are float?","document_hash":"fixture"}
            result=query(request,profile,[],db)
            self.assertEqual(result["table"]["count"],39)
            self.assertEqual(len(result["citations"]),3)
            selected=query(dict(request,subsystem="Subsystem-3"),profile,[],db)
            self.assertEqual(selected["table"]["count"],13)
            self.assertEqual(selected["table"]["rows"][0]["signal"],"ID_39")
            followed=query(dict(request,question="Show all subsystems",subsystem="Subsystem-3"),
                           profile,[{"role":"assistant","result":selected}],db)
            self.assertEqual(followed["table"]["count"],39)
            db.close()

    def test_compact_header_and_zero_based_fields(self):
        header = [["MESSAGE ID", "00102", "RT ADDRESS", "01"],
                  ["SUBADDR", "02", "MSG TYPE", "TRANSMIT"], ["WORD COUNT", "32", "", ""]]
        fields = [["Word No.", "Signal ID", "Unit", "Word Format", "Data type"],
                  ["0-1", "ANGLE", "rad", "2words", "Float"],
                  ["2", "FLAGS", "count", "1word", "USI"],
                  ["31", "SPARE", "-", "1word", "-"]]
        context = {}
        rows = worker.parse_message_layout([header, fields], 1, "digital", "fixture", context, ["Subsystem-2"])
        self.assertEqual(rows[0]["id"], "Subsystem-2 / 00102 / ANGLE")
        self.assertEqual((rows[0]["draft_cells"]["data_word"], rows[0]["draft_cells"]["width"]), (1, 32))
        self.assertNotIn("word_order", rows[0]["draft_cells"])
        self.assertNotIn("scale", rows[0]["draft_cells"])
        self.assertEqual((rows[1]["draft_cells"]["data_word"], rows[1]["draft_cells"]["signed"]), (3, False))
        self.assertNotIn("draft_cells", rows[2])
        continued = worker.parse_message_layout([[["3-4", "ANGLE_2", "rad", "2words", "Float"]]],
                                                2, "digital", "next-file", context, ["Subsystem-2"], continuation=True)
        self.assertEqual(continued[0]["draft_cells"]["data_word"], 4)
        self.assertEqual(continued[0]["source_hash"], "next-file")

    def test_format_defaults_and_bitwise_review(self):
        headers = ["Name", "RT", "SA", "Direction", "Word number", "LSB", "Width",
                   "Scale", "Offset", "Unit", "Type", "Word order", "Mask", "Right shift"]
        rows = [
            ["SPEED", "3", "2", "Rx", "1", "", "", "unscaled", "", "m/s", "IEEE 754 32", "LSW first", "", ""],
            ["DISTANCE", "3", "2", "Rx", "3", "", "", "1", "0", "m", "double", "", "", ""],
            ["COUNT", "3", "2", "Rx", "7", "", "", "1", "0", "count", "int16", "", "", ""],
            ["MODE", "3", "2", "Rx", "8", "", "", "", "", "", "enum", "", "0x00C0", "6"],
            ["FAULT", "3", "2", "Rx", "8", "4", "", "", "", "", "flag", "", "", ""],
            ["UNKNOWN", "3", "2", "Rx", "9", "", "", "", "", "", "enum", "", "0x0050", "4"],
        ]
        mappings, pending = worker.parse_table([headers, *rows], 1, "digital", "hash", "")
        self.assertEqual([row["id"] for row in mappings], ["SPEED", "COUNT", "MODE", "FAULT"])
        self.assertEqual((mappings[0]["width"], mappings[0]["lsb"], mappings[0]["lsw_first"]), (32, 0, True))
        self.assertTrue(mappings[1]["signed"])
        self.assertEqual((mappings[2]["lsb"], mappings[2]["width"]), (6, 2))
        self.assertEqual((mappings[3]["lsb"], mappings[3]["width"]), (4, 1))
        self.assertEqual(pending[0]["draft_cells"]["width"], 64)
        self.assertNotIn("word_order", pending[0]["draft_cells"])
        self.assertIn("Non-contiguous", pending[1]["reason"])
        self.assertNotIn("lsb", pending[1]["draft_cells"])

    def test_partial_table_preserves_explicit_fields_and_conflicts(self):
        headers = ["Signal", "RT", "SA", "Direction", "Word number", "Word count", "LSB", "Width",
                   "Scale", "Offset", "Unit", "Type", "Word order"]
        row = ["SPEED", "3", "12", "Rx", "1", "32", "0", "32", "1/8", "", "m/s", "uint32", "MSW first"]
        rules, drafts = worker.parse_table([headers, row], 1, "digital", "hash", "")
        self.assertFalse(rules)
        fields = drafts[0]["draft_cells"]
        self.assertEqual((fields["id"], fields["word_count"], fields["data_word"]), ("SPEED", 32, 1))
        self.assertEqual((fields["representation"], fields["signed"], fields["word_order"]), ("integer", False, "msw-first"))
        self.assertEqual(fields["scale"], 0.125)
        self.assertNotIn("offset", fields)
        headers[4] = "Word"
        row[9] = "0"
        rules, drafts = worker.parse_table([headers, row], 1, "digital", "hash",
                                          "Word indexing is zero-based. Word indexing is one-based.")
        self.assertFalse(rules)
        self.assertNotIn("data_word", drafts[0]["draft_cells"])
        headers[4] = "Word number"
        headers.append("Signedness")
        row.append("signed")
        rules, drafts = worker.parse_table([headers, row], 1, "digital", "hash", "")
        self.assertFalse(rules)
        self.assertIn("conflicts", drafts[0]["reason"])
        self.assertNotIn("signed", drafts[0]["draft_cells"])


    def test_supplied_example_is_review_only(self):
        source = Path(__file__).resolve().parents[2] / "Example_PDF_Tables_Useful_For_Engineering_Conversions.md"
        if not source.exists():
            self.skipTest("Local example is not distributed with the source")
        lines = source.read_text(encoding="utf-8").splitlines()
        tables, rows = [], []
        for line in lines:
            if line.startswith("|"):
                cells = [cell.strip().strip("`") for cell in line.strip("|").split("|")]
                if all(set(cell) <= set("-: ") for cell in cells):
                    continue
                rows.append(cells)
            elif rows:
                tables.append(rows); rows = []
        if rows: tables.append(rows)
        state = {}
        draft = worker.parse_message_layout(tables, 1, "digital", "hash", state)
        self.assertEqual(len(draft), 18)
        self.assertEqual(state["message_id"], "01001")
        self.assertEqual((state["rt"], state["sa"], state["word_count"]), (3, 12, 32))
        self.assertEqual((draft[0]["draft_field"]["word_index"], draft[0]["draft_field"]["word_span"]), (0, 2))
        self.assertEqual((draft[6]["draft_field"]["word_first"], draft[6]["draft_field"]["word_last"]), (10, 13))
        self.assertFalse(any("candidate" in row for row in draft))


def fixture_pdf(path, temperature_unit="degC", *, headings=(), include_header=True, signals=None):
    headers = ["Name", "RT", "SA", "Direction", "Word index (0-based)", "LSB", "Width", "Scale", "Offset", "Unit", "Type", "Word order"]
    rows = [headers, ["TEMP", "3", "2", "Rx", "0", "0", "16", "0.01", "0", temperature_unit, "uint16", "MSW first"],
            ["PRESSURE", "3", "2", "Rx", "1", "0", "16", "0.1", "5", "kPa", "uint16", "MSW first"],
            ["COUNTER", "3", "2", "Rx", "2", "0", "32", "1", "0", "count", "uint32", "MSW first"]]
    if signals is not None:
        rows = [headers, *signals]
    if not include_header:
        rows = rows[1:]
    widths = [105, 28, 28, 65, 140, 30, 45, 55, 50, 42, 65, 75]
    x_positions = [30]
    for width in widths:
        x_positions.append(x_positions[-1] + width)
    stream = ["0.4 w", "BT /F1 14 Tf 30 445 Td (ICD engineering conversion acceptance fixture) Tj ET",
              f"BT /F1 10 Tf 30 420 Td (Temperature TEMP is measured in {temperature_unit}. Word indices are zero-based. LSB = 0.) Tj ET"]
    for index, heading in enumerate(headings):
        stream.append(f"BT /F1 10 Tf 30 {483-18*index} Td ({heading}) Tj ET")
    for x in x_positions:
        stream.append(f"{x} {385-len(rows)*35} m {x} 385 l S")
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
        offline = mock.patch("socket.socket.connect", side_effect=OSError("Network disabled for PDF tests"))
        offline.start()
        self.addCleanup(offline.stop)

    def tearDown(self):
        self.temp.cleanup()

    def ingest(self, path):
        with contextlib.redirect_stdout(io.StringIO()):
            return worker.ingest({"pdf": str(path), "store": str(self.root / "store")})

    def test_explicit_status_and_ambiguous_response_definition(self):
        header = ["Signal", "RT", "SA", "Direction", "Source word", "Peer RT", "Peer SA",
                  "LSB", "Width", "Type", "Unit", "Scale", "Offset"]
        valid = ["TX_BUSY", "3", "2", "transmit", "Status 1", "", "", "3", "1", "uint", "flag", "1", "0"]
        ambiguous = ["UNKNOWN", "3", "2", "transmit", "Response word", "", "", "3", "1", "uint", "flag", "1", "0"]
        mapped, drafts = worker.parse_table([header, valid, ambiguous], 2, "digital", "digest", "")
        self.assertEqual(len(mapped), 1)
        self.assertEqual(mapped[0]["source_word"], "status1")
        self.assertEqual(mapped[0]["word"], 0)
        self.assertEqual(len(drafts), 1)
        self.assertIn("explicitly", drafts[0]["reason"])

    def test_digital_pdf_mapping_retrieval_and_idempotence(self):
        path = self.root / "reference.pdf"
        fixture_pdf(path)
        result = self.ingest(path)
        profile = result["profile"]
        self.assertFalse(profile["automatic"], profile)
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
        fast = worker.ask({"store": str(self.root / "store"), "question": "Temperature TEMP unit",
                           "fast_only": True, "prepare_generation": True})
        self.assertEqual(fast["retrieval"], "Table evidence + BM25")
        self.assertFalse(fast.get("needs_generation"))
        self.assertIn("degC", fast["answer"])
        self.assertEqual(fast["citations"][0]["hash"], profile["sha256"])
        missing = worker.ask({"store": str(self.root / "store"), "question": "What is the unfindable_xyz?"})
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

    def test_ordered_set_continuation_conflicts_and_published_chat(self):
        first, continued, other = [self.root / name for name in ("first.pdf", "next.pdf", "other.pdf")]
        temp = ["TEMP", "3", "2", "Rx", "0", "0", "16", "0.01", "0", "degC", "uint16", "MSW first"]
        fault = ["FAULT", "3", "2", "Rx", "1", "0", "1", "1", "0", "unitless", "flag", ""]
        conflict = temp.copy()
        conflict[7] = "0.05"
        internal = temp.copy()
        internal[1] = "8"
        fixture_pdf(first, headings=("Subsystem: External", "Message: Health"), signals=[temp, fault])
        conflicting_flag = fault.copy()
        conflicting_flag[5] = "1"
        fixture_pdf(continued, headings=("Table continued",), include_header=False, signals=[conflicting_flag])
        fixture_pdf(other, headings=("Subsystem: Internal", "Message: Health"), signals=[internal, conflict[:1]+["8"]+conflict[2:]])
        store = str(self.root / "store")
        with contextlib.redirect_stdout(io.StringIO()):
            result = worker.ingest(dict(pdfs=list(map(str, [first, continued, other])), set_name="Flight", store=store))
        profile = result["profile"]
        self.assertEqual(profile["document"], "Flight")
        self.assertEqual(profile["signals"][0]["subsystem"],"External")
        self.assertEqual(profile["signals"][0]["message_id"],"Health")
        self.assertEqual(profile["pages"], 3)
        self.assertEqual([row["id"] for row in profile["signals"]], ["External / Health / TEMP"])
        self.assertEqual({row["source_document"] for row in profile["review"]}, {first.name, continued.name, other.name})
        self.assertTrue(all("Conflicting definitions" in row["reason"] for row in profile["review"]))
        request = dict(store=store, document_hash=profile["sha256"], question="TEMP scale", answer_source="published")
        published = dict(profile["signals"][0], scale=.025)
        request["active_profile"] = {"signals": [published, dict(published, id="Internal / Health / TEMP", rt=8)]}
        self.assertEqual(worker.ask(request)["mode"], "clarification")
        answer = worker.ask(dict(request, question="External Health TEMP scale"))
        self.assertIn("scale 0.025", answer["answer"])
        self.assertIn("0.01", answer["answer"])
        self.assertEqual(answer["citations"][0]["document"], first.name)
        self.assertEqual(worker.library(request)["profile"], profile)

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


class RetrievalContextTests(unittest.TestCase):
    def test_table_scope_lazy_index_and_followup(self):
        import sqlite3
        import retrieval_context as retrieval
        db=sqlite3.connect(":memory:")
        retrieval.initialize(db)
        def row(group, message, unit, page):
            return dict(subsystem=group,message_id=message,source_signal_id="ID_1",
                        source_hash="source",source_page=page,evidence={"text":"ID_1 float32 pressure in "+unit})
        rows=[row("S1","04102","bar",1),row("S3","04102","psi",3),row("S1","08012","Pa",4)]
        found=retrieval.lookup(db,"set",rows,"pressure","S1","message 04102 pressure")
        self.assertEqual(len(found),1)
        self.assertEqual(found[0][1],1)
        self.assertIn("S1 / 04102 / ID_1",found[0][2])
        self.assertEqual(len(retrieval.lookup(db,"set",rows,"pressure","","pressure")),3)
        # Reindexing is additive to library storage and replaces only this set's evidence.
        retrieval.index(db,"other",[row("S2","02102","kPa",2)])
        retrieval.index(db,"set",[row("S1","04102","mbar",5)])
        self.assertEqual(retrieval.lookup(db,"set",rows,"pressure","S1","pressure")[0][1],5)
        self.assertEqual(retrieval.lookup(db,"other",rows,"pressure","S2","pressure")[0][1],2)
        context=[dict(role="assistant",result={"table":{"rows":[dict(subsystem="S1",message_id="04102",signal="ID_1")]}})]
        self.assertIn("S1 04102 ID_1",retrieval.followup("What is its unit?",context))
        self.assertEqual(retrieval.followup("Which words are float?",context),"Which words are float?")
        db.close()


class LibraryHistoryTests(unittest.TestCase):
    def test_permanent_delete_keeps_history_and_shared_pdf(self):
        with tempfile.TemporaryDirectory() as folder:
            store = Path(folder)
            from library_history import operate
            db = worker.database(store)
            first, shared, revision, other = ("a"*64, "b"*64, "c"*64, "d"*64)
            for digest in (first, shared, revision, other):
                db.execute("INSERT INTO documents VALUES(?,?,1)", (digest, digest))
            for digest in (first, shared):
                (store / (digest + ".pdf")).write_bytes(b"owned copy")
                db.execute("INSERT INTO chunks VALUES(?,1,'indexed text')", (digest,))
            for digest in (revision, other):
                db.execute("INSERT INTO document_profiles VALUES(?,?)", (digest, "{}"))
            db.executemany("INSERT INTO icd_set_members VALUES(?,?,?)",
                           [(revision, first, 0), (revision, shared, 1), (other, shared, 0)])
            db.commit()
            chat = operate(db, dict(op="conversation_new", document_hash=revision))
            db.close()
            request = dict(store=folder, document_hash=revision)
            self.assertEqual(worker.permanent_delete(request, preview=True)["purge_hashes"], [first])
            self.assertFalse(worker.permanent_delete(request)["cleanup_pending"])
            self.assertFalse((store / (first + ".pdf")).exists())
            self.assertTrue((store / (shared + ".pdf")).exists())
            worker.permanent_delete(request)
            db = worker.database(store)
            self.assertFalse(db.execute("SELECT 1 FROM chunks WHERE hash=?", (first,)).fetchone())
            self.assertTrue(db.execute("SELECT 1 FROM chunks WHERE hash=?", (shared,)).fetchone())
            self.assertTrue(operate(db, dict(op="conversation_load", conversation_id=chat["conversation_id"]))["source_deleted"])
            db.close()

    def test_threads_archive_restore_and_legacy_migration(self):
        import sqlite3
        from library_history import operate, history
        with tempfile.TemporaryDirectory() as folder:
            db=sqlite3.connect(Path(folder)/"documents.sqlite")
            db.execute("CREATE TABLE chat_messages(id INTEGER PRIMARY KEY,hash TEXT,role TEXT,content TEXT,utc TEXT)")
            digest="a"*64
            db.executemany("INSERT INTO chat_messages(hash,role,content,utc) VALUES(?,?,?,?)",[(digest,"user",str(i),"old") for i in range(85)])
            db.commit();db.close();db=worker.database(folder)
            db.execute("INSERT INTO documents VALUES(?,?,?)",(digest,"First ICD",1))
            db.execute("INSERT INTO document_profiles VALUES(?,?)",(digest,json.dumps({"signals":[],"review":[]})));db.commit()
            recent=history(db,digest);older=history(db,digest,before=recent[0]["message_id"])
            self.assertEqual(len(recent)+len(older),85)
            first=operate(db,dict(op="conversation_new",document_hash=digest,subsystem="Subsystem-1",answer_source="published"))["conversation_id"]
            second=operate(db,dict(op="conversation_new",document_hash=digest,subsystem="Subsystem-3"))["conversation_id"]
            db.close()
            result=worker.remember_answer(dict(store=folder,conversation_id=first),digest,"First question",dict(answer="Only first thread",citations=[],table={"rows":[{"signal":"ID_1"}]}))
            self.assertEqual(result["conversation_id"],first)
            db=worker.database(folder)
            self.assertEqual(history(db,digest,second),[])
            loaded=operate(db,dict(op="conversation_load",conversation_id=first))
            self.assertEqual(loaded["subsystem"],"Subsystem-1")
            self.assertEqual(loaded["history"][-1]["result"]["table"]["rows"][0]["signal"],"ID_1")
            operate(db,dict(op="library_rename",document_hash=digest,name="Renamed ICD"))
            operate(db,dict(op="library_archive",document_hash=digest));db.close()
            self.assertEqual(worker.library(dict(store=folder))["documents"],[])
            self.assertTrue(worker.library(dict(store=folder,document_hash=digest))["archived"])
            db=worker.database(folder);self.assertEqual(len(history(db,digest,first)),2)
            operate(db,dict(op="library_restore",document_hash=digest));db.close()
            self.assertEqual(worker.library(dict(store=folder))["documents"][0]["name"],"Renamed ICD")

    def test_reference_answers_are_not_pdf_evidence(self):
        from engineering_reference import answer
        result=answer({"question":"What is IEEE 754 binary32?"})
        self.assertEqual(result["mode"],"engineering-reference")
        self.assertIn("23 fraction bits",result["answer"])
        self.assertEqual(result["citations"][0]["hash"],"")
        self.assertIsNone(answer({"question":"Which words are float?"}))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--write-fixture":
        destination = Path(sys.argv[2]); destination.parent.mkdir(parents=True, exist_ok=True)
        fixture_pdf(destination)
    else:
        unittest.main(verbosity=2)
