"""Render the supplied table as a searchable, single-page PDF without dependencies."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
source = root / "Example_PDF_Tables_Useful_For_Engineering_Conversions.md"
destination = root / "examples" / "demo-icd" / "Engineering-32-Word-Example.pdf"
tables, rows = [], []
for line in source.read_text(encoding="utf-8").splitlines():
    if line.startswith("|"):
        cells = [part.strip().strip("`") for part in line.strip("|").split("|")]
        if all(re.fullmatch(r"[-: ]+", cell) for cell in cells):
            continue
        rows.append(cells)
    elif rows:
        tables.append(rows)
        rows = []
if rows:
    tables.append(rows)
if len(tables) != 2 or len(tables[0]) != 2 or len(tables[1]) != 21:
    raise ValueError("Unexpected example table structure")

commands = ["0.5 w"]
def text(x, y, value, size=10):
    encoded = value.encode("cp1252").hex()
    commands.append(f"BT /F1 {size} Tf {x} {y} Td <{encoded}> Tj ET")
def table(data, xs, top, height):
    bottom = top - len(data)*height
    for x in xs: commands.append(f"{x} {bottom} m {x} {top} l S")
    for row in range(len(data)+1):
        y = top-row*height
        commands.append(f"{xs[0]} {y} m {xs[-1]} {y} l S")
    for row, values in enumerate(data):
        for col, value in enumerate(values): text(xs[col]+5, top-row*height-height+5, value, 9)
text(36, 805, "MIL-STD-1553 Message Layout", 16)
text(36, 780, "Message Header", 12)
table(tables[0], [36, 125, 215, 325, 430, 575], 770, 19)
text(36, 715, "Data Word Mapping", 12)
table(tables[1], [36, 160, 360, 575], 704, 24)
stream = ("\n".join(commands)+"\n").encode("ascii")
objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
           b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 842] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
           b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
           f"<< /Length {len(stream)} >>\nstream\n".encode()+stream+b"endstream"]
output=bytearray(b"%PDF-1.4\n"); offsets=[0]
for i, obj in enumerate(objects, 1):
    offsets.append(len(output));output+=f"{i} 0 obj\n".encode()+obj+b"\nendobj\n"
xref=len(output);output+=f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode()
for offset in offsets[1:]: output+=f"{offset:010} 00000 n \n".encode()
output+=f"trailer << /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
destination.write_bytes(output)
print(destination)
