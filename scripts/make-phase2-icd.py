"""Create the synthetic Thermal Bench Assistant acceptance documents."""
from pathlib import Path
import argparse

from PIL import Image, ImageDraw, ImageFont
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.pdfgen import canvas
from reportlab.platypus import Table, TableStyle


def contents(revision):
    offset = -10 if revision == "B" else -20
    return [
        ["Signal", "Data word", "Type", "Order", "Scale", "Offset", "Unit"],
        ["Temperature", "1", "uint16", "MSW first", "0.5", str(offset), "degC"],
        ["Signed correction", "2", "int16", "MSW first", "1", "0", "count"],
        ["Reference pressure", "3-4", "float32", "MSW first", "1", "0", "kPa"],
    ], [
        f"Temperature conversion: unsigned word 1 times 0.5 plus ({offset}) degrees Celsius.",
        f"Temperature example: raw 0064 decodes to 100 and converts to {50 + offset} degC.",
        "Signed correction uses two's complement. Raw FFFE represents -2 count.",
        "Reference pressure uses IEEE float32, most significant word first. 4148 0000 is 12.5 kPa.",
        "Nonlinear index uses unsigned word 5. Formula: x * x * 0.01.",
        "Nonlinear index examples: 000A gives 1.0 and 0014 gives 4.0.",
        "Source samples are synthetic. Use only for software verification.",
    ]


def digital(path, revision):
    rows, evidence = contents(revision)
    pdf = canvas.Canvas(str(path), pagesize=letter, invariant=1)
    pdf.setTitle(f"Thermal Bench ICD Rev {revision}")
    pdf.setFont("Helvetica-Bold", 20)
    pdf.drawString(54, 717, f"Thermal Bench ICD | Revision {revision}")
    pdf.setFont("Helvetica", 12)
    pdf.drawString(54, 683, "Subsystem: Thermal Bench | Message: Bench temperature readings")
    pdf.drawString(54, 658, "RT 7 | Subaddress 12 | BC to RT | Channel 1 | Bus A | 5 payload words")
    pdf.drawString(54, 627, "Synthetic data for desktop validation.")
    pdf.showPage()
    pdf.setFont("Helvetica-Bold", 17)
    pdf.drawString(54, 717, "Table 1. Thermal Bench payload definitions")
    table = Table(rows, colWidths=[128, 62, 59, 68, 48, 48, 52])
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e4eff4")),
        ("GRID", (0, 0), (-1, -1), 0.6, colors.HexColor("#234658")),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
    ]))
    _, height = table.wrap(540, 670)
    table.drawOn(pdf, 54, 676-height)
    pdf.setFont("Helvetica", 11)
    for index, line in enumerate(evidence):
        pdf.drawString(54, 555-28*index, line)
    pdf.showPage()
    pdf.save()


def scanned(path):
    fonts = Path("C:/Windows/Fonts")
    title = ImageFont.truetype(str(fonts / "arialbd.ttf"), 58)
    regular = ImageFont.truetype(str(fonts / "arial.ttf"), 40)
    first = Image.new("RGB", (1800, 2300), "white")
    draw = ImageDraw.Draw(first)
    draw.text((100, 125), "Thermal Bench ICD | Revision B", font=title, fill="#172b3a")
    draw.text((100, 250), "RT 7 | Subaddress 12 | BC to RT | Channel 1 | Bus A", font=regular, fill="black")
    draw.text((100, 330), "Subsystem: Thermal Bench | 5 payload words", font=regular, fill="black")
    second = Image.new("RGB", first.size, "white")
    draw = ImageDraw.Draw(second)
    draw.text((100, 125), "Table 1. Thermal Bench payload definitions", font=title, fill="#172b3a")
    for number, line in enumerate(contents("B")[1]):
        draw.text((100, 260+100*number), line, font=regular, fill="black")
    first.save(path, format="PDF", resolution=216, save_all=True, append_images=[second])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    root = parser.parse_args().directory
    root.mkdir(parents=True, exist_ok=True)
    digital(root / "Thermal-Bench-ICD-Rev-B.pdf", "B")
    digital(root / "Thermal-Bench-ICD-Rev-A.pdf", "A")
    scanned(root / "Thermal-Bench-ICD-Rev-B-Scan.pdf")
