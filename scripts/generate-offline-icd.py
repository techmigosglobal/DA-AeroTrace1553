"""Generate the single documented offline fixture ICD shipped with AeroTrace."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
destination = root / "examples" / "offline-icd" / "Offline-Simulation-ICD.pdf"

rows = [["Signal", "RT", "SA", "Direction", "Word count", "Word number", "LSB",
         "Width", "Type", "Word order", "Scale", "Offset", "Unit"]]
fields = [
    (1, 32, "float32", "km/hr"), (3, 32, "float32", "km/hr"), (5, 32, "float32", "km/hr"),
    (7, 16, "int16", "degree"), (8, 16, "int16", "degree"), (9, 16, "int16", "degree"),
    (10, 64, "float64", "m/s²"), (14, 64, "float64", "m/s²"), (18, 64, "float64", "m/s²"),
]
fields += [(word, 16, "int16" if 25 <= word <= 27 else "uint16",
            "m" if word <= 24 else "s" if word >= 28 else "unitless")
           for word in range(22, 31)]
for word, width, datatype, unit in fields:
    rows.append([f"RT03_WORD_{word:02}", "3", "12", "Rx", "32", str(word), "0", str(width),
                 datatype, "MSW first", "1", "0", unit])
rows.append(["RT08_TEST_VALUE", "8", "12", "Rx", "32", "1", "0", "32",
             "uint32", "LSW first", "0.25", "-10", "unitless"])

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
text(28, 563, "AeroTrace Offline Simulation ICD", 17)
text(28, 542, "Simulation only. Both routes carry 32 data words. Word numbers are one-based.", 11)
table(rows, [28, 145, 170, 195, 254, 322, 400, 428, 465, 518, 586, 624, 672, 762], 524, 19)
text(28, 122, "RT03 first packet: words 1-2 = 42C8 0000, engineering value = 100 km/hr.", 11)
text(28, 101, "RT08 first packet: words 1-2 = 07D0 0000, engineering value = 2000 x 0.25 - 10 = 490.", 11)
text(28, 80, "RT08 raw value increases by one per full route cycle and wraps after 400 increments.", 11)
stream = ("\n".join(commands)+"\n").encode("ascii")
objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
           b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 792 612] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
           b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
           f"<< /Length {len(stream)} >>\nstream\n".encode()+stream+b"endstream"]
output=bytearray(b"%PDF-1.4\n"); offsets=[0]
for i, obj in enumerate(objects, 1):
    offsets.append(len(output));output+=f"{i} 0 obj\n".encode()+obj+b"\nendobj\n"
xref=len(output);output+=f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode()
for offset in offsets[1:]: output+=f"{offset:010} 00000 n \n".encode()
output+=f"trailer << /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_bytes(output)
print(destination)
