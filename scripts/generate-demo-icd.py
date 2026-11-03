"""Generate a deterministic local ICD input kit; never configures real hardware."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import pypdfium2 as pdfium


class DemoDocument:
    def __init__(self):
        self.stream = []

    def new_page(self, width, height):
        self.width, self.height = width, height
        return self

    def insert_text(self, point, value, fontsize=11, color=(0.15, 0.2, 0.16)):
        value = value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        x, y = point
        self.stream.append(f"{color[0]} {color[1]} {color[2]} rg BT /F1 {fontsize} Tf {x} {self.height-y} Td ({value}) Tj ET")

    def draw_line(self, first, second, color, width):
        self.stream.append(f"{color[0]} {color[1]} {color[2]} RG {width} w {first[0]} {self.height-first[1]} m {second[0]} {self.height-second[1]} l S")

    def save(self, path):
        content = "\n".join(self.stream).encode("ascii")
        objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                   f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {self.width} {self.height}] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>".encode(),
                   b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>", f"<< /Length {len(content)} >>\nstream\n".encode()+content+b"\nendstream"]
        output = bytearray(b"%PDF-1.4\n"); offsets = [0]
        for index, obj in enumerate(objects, 1):
            offsets.append(len(output)); output += f"{index} 0 obj\n".encode()+obj+b"\nendobj\n"
        start = len(output); output += f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode()
        for offset in offsets[1:]:
            output += f"{offset:010} 00000 n \n".encode()
        output += f"trailer << /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n".encode()
        path.write_bytes(output)


def generate(destination):
    destination.mkdir(parents=True, exist_ok=True)
    names = ["TEMP", "PRESSURE", "VOLTAGE", "VIBRATION", "RPM", "CURRENT", "PITCH", "STATUS", "READY"]
    units = ["degC", "kPa", "V", "g", "RPM", "A", "deg", "bits", "state"]
    words = [1000, 6699, 2850, 242, 33555, 418, 1245, 8]
    signals = []
    for i, (name, unit) in enumerate(zip(names, units)):
        signals.append(dict(id=name, unit=unit, rt=3, sa=2, transmit=False,
                            word=min(i, 7), lsb=3 if i == 8 else 0,
                            width=1 if i == 8 else 16, signed=False,
                            scale=0.1 if i == 4 else 1 if i >= 7 else 0.01,
                            offset=0, channel=0, bus=-1, numeric_type="integer", lsw_first=False))
    doc = DemoDocument()
    page = doc.new_page(width=960, height=690)
    page.insert_text((30, 37), "AeroTrace | Demonstration ICD | AT-DEMO-ICD-001 revision 1", fontsize=17, color=(.15, .2, .16))
    notes = ["SYNTHETIC INPUT ONLY - not an aircraft or hardware acceptance ICD.",
             "MIL-STD-1553: RT 3, SA 2, BC-to-RT (Rx), 8 data words. Word indices are zero-based.",
             "Unsigned binary fields; LSB 0 is the least-significant bit. Engineering = extracted integer * scale + offset.",
             "MSW first. Rules match either channel and bus; select a single channel/bus when plotting."]
    for i, line in enumerate(notes):
        page.insert_text((30, 65+i*18), line, fontsize=11)
    headers = ["Name", "RT", "SA", "Direction", "Word index (0-based)", "LSB", "Width", "Scale", "Offset", "Unit", "Type", "Word order"]
    widths = [100, 28, 28, 63, 135, 30, 45, 55, 50, 48, 65, 75]
    xpos = [30]
    for width in widths:
        xpos.append(xpos[-1]+width)
    rows = [headers] + [[s["id"], 3, 2, "Rx", s["word"], s["lsb"], s["width"], s["scale"], 0, s["unit"], "uint16", "MSW first"] for s in signals]
    top, row_height = 150, 31
    for x in xpos:
        page.draw_line((x, top), (x, top+row_height*len(rows)), color=(.45, .5, .45), width=.5)
    for row_index in range(len(rows)+1):
        y = top+row_index*row_height
        page.draw_line((xpos[0], y), (xpos[-1], y), color=(.45, .5, .45), width=.5)
    for row_index, row in enumerate(rows):
        for col, value in enumerate(row):
            page.insert_text((xpos[col]+4, top+row_index*row_height+20), str(value), fontsize=11)
    examples = ["Worked payload (hex): 03E8 1A2B 0B22 00F2 8313 01A2 04DD 0008",
                "Expected: TEMP 10 degC; PRESSURE 66.99 kPa; VOLTAGE 28.5 V; VIBRATION 2.42 g;",
                "RPM 3355.5; CURRENT 4.18 A; PITCH 12.45 deg; STATUS 8; READY 1 (word 7 bit 3).",
                "TEMP is temperature measured in degC. PRESSURE is hydraulic pressure measured in kPa.",
                "Time display: HH:MM:SS.mmm.uuu. It is relative device time, not automatically UTC.",
                "Repeated timestamps can be valid at the configured card resolution; gaps must not be filled or hidden.",
                "PDF ingestion builds a local evidence index and conversion profile. It does not retrain model weights."]
    for i, line in enumerate(examples):
        page.insert_text((30, 494+i*20), line, fontsize=11)
    pdf = destination/"AeroTrace-Demo-ICD.pdf"
    doc.save(pdf)
    digest = hashlib.sha256(pdf.read_bytes()).hexdigest()
    for signal in signals:
        signal.update(source_document=pdf.name, source_hash=digest, source_page=1)
    (destination/"AeroTrace-Demo-ICD.json").write_text(json.dumps(dict(schema="aerotrace.icd.profile.v2", signals=signals), indent=2), encoding="utf-8")
    with (destination/"expected-conversions.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(["signal", "word", "raw_hex", "engineering", "unit", "card_time_example"])
        for signal in signals:
            raw = words[signal["word"]]
            value = ((raw >> signal["lsb"]) & ((1 << signal["width"])-1))*signal["scale"]
            writer.writerow([signal["id"], signal["word"], f"{raw:04X}", value, signal["unit"], "00:00:01.234.567"])
    # An image-only input exercises OCR independently of the digital text layer.
    with pdfium.PdfDocument(pdf) as rendered:
        raster_page = rendered[0]; bitmap = raster_page.render(scale=3)
        bitmap.to_pil().convert("RGB").save(destination/"AeroTrace-Demo-ICD-scanned.pdf", "PDF", resolution=216, quality=95)
        bitmap.close(); raster_page.close()
    print(json.dumps(dict(pdf=str(pdf), signals=len(signals), sha256=digest)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    generate(parser.parse_args().destination)
