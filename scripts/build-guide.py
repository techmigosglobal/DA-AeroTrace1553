"""Build the offline HTML guide; optionally a PDF with ReportLab (build tooling only)."""
from pathlib import Path
import html
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / 'docs'


def inline(text):
    text = html.escape(text)
    text = re.sub(r'`([^`]+)`', r'<code>\1</code>', text)
    return re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)


def blocks(text):
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line:
            i += 1
            continue
        if line.startswith('#'):
            yield ('heading', len(line) - len(line.lstrip('#')), line.lstrip('#').strip())
        elif line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r'[:\- ]+', c) for c in cells):
                    rows.append(cells)
                i += 1
            yield ('table', rows)
            continue
        elif re.match(r'^\d+\. ', line):
            yield ('list', re.sub(r'^\d+\. ', '', line), re.match(r'^\d+', line)[0])
        else:
            paragraph = [line]
            while i + 1 < len(lines) and lines[i+1].strip() and not re.match(r'^(#|\||\d+\. )', lines[i+1].strip()):
                i += 1
                paragraph.append(lines[i].strip())
            yield ('paragraph', ' '.join(paragraph))
        i += 1


def main():
    content = list(blocks((DOCS/'Operator-Guide.md').read_text(encoding='utf-8')))
    assets = DOCS/'assets'
    assets.mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'apps/desktop/icons/aerotrace.png', assets/'aerotrace.png')
    toc, body = [], []
    for index, block in enumerate(content):
        kind = block[0]
        if kind == 'heading':
            _, level, title = block
            body.append(f'<h{level} id="section-{index}">{inline(title)}</h{level}>')
            if level == 2:
                toc.append(f'<li><a href="#section-{index}">{inline(title)}</a></li>')
        elif kind == 'table':
            rows = block[1]
            body.append('<div class="table-scroll"><table>')
            for r, cells in enumerate(rows):
                tag = 'th' if r == 0 else 'td'
                body.append('<tr>'+''.join(f'<{tag}>{inline(c)}</{tag}>' for c in cells)+'</tr>')
            body.append('</table></div>')
        elif kind == 'list':
            body.append(f'<p class="step"><b>{block[2]}.</b> {inline(block[1])}</p>')
        else:
            body.append('<p>'+inline(block[1])+'</p>')
    template = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AeroTrace 1553 — Operator guide</title><style>
    body{font:16px/1.65 "Segoe UI",system-ui,sans-serif;color:#272d29;background:#f4f5f1;margin:0}main{max-width:1020px;margin:auto;background:white;padding:48px}header{display:flex;align-items:center;gap:16px;border-bottom:2px solid #244f3d;padding-bottom:24px}header img{width:60px;height:60px}nav{background:#f3f6f1;padding:18px 24px;margin:28px 0}nav ul{columns:2;list-style:none;padding:0}a{color:#244f3d}h1{font-size:30px;line-height:1.3}h2{font-size:23px;margin-top:46px;border-bottom:1px solid #dce1db;padding-bottom:8px}h3{font-size:18px;margin-top:28px}code{background:#f0f3ef;padding:2px 4px;font:90% Consolas,monospace;overflow-wrap:anywhere}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;margin:20px 0}th,td{text-align:left;vertical-align:top;padding:10px 12px;border:1px solid #dce1db}th{background:#244f3d;color:white}th code{color:#272d29}tr:nth-child(even){background:#f7f8f5}.step{padding-left:15px}small{color:#657066}@media(max-width:700px){main{padding:20px}nav ul{columns:1}}@media print{body{background:white;font-size:10pt}main{max-width:none;padding:0}nav ul{columns:2}h2,h3{break-after:avoid}tr{break-inside:avoid}a{color:inherit}header{padding-bottom:12px}}
    </style></head><body><main><header><img src="assets/aerotrace.png" alt="AeroTrace flight and signal mark"><div><b>AeroTrace 1553</b><br><small>Offline operator guide · 0.6.0 · Windows x64</small></div></header><nav aria-label="Contents"><b>Contents</b><ul>TOC</ul></nav>BODY</main></body></html>'''
    (DOCS/'Operator-Guide.html').write_text(template.replace('TOC', ''.join(toc)).replace('BODY', '\n'.join(body)), encoding='utf-8')
    if '--pdf' in sys.argv:
        from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image
        from reportlab.lib import colors
        from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
        from reportlab.lib.pagesizes import A4
        from reportlab.pdfbase import pdfmetrics
        from reportlab.pdfbase.ttfonts import TTFont
        fonts = Path('C:/Windows/Fonts')
        pdfmetrics.registerFont(TTFont('Guide', str(fonts/'segoeui.ttf')))
        pdfmetrics.registerFont(TTFont('GuideBold', str(fonts/'seguisb.ttf')))
        pdfmetrics.registerFontFamily('Guide', normal='Guide', bold='GuideBold', italic='Guide', boldItalic='GuideBold')
        styles = getSampleStyleSheet()
        styles.add(ParagraphStyle(name='GuideBody', fontName='Guide', fontSize=10, leading=14, spaceAfter=9))
        styles.add(ParagraphStyle(name='GuideCell', parent=styles['GuideBody'], fontSize=8.5, leading=12, spaceAfter=0))
        styles.add(ParagraphStyle(name='GuideHeaderCell', parent=styles['GuideCell'], textColor=colors.white, fontName='GuideBold'))
        for name in ('Title', 'Heading1', 'Heading2'):
            styles[name].fontName='GuideBold'
            styles[name].textColor=colors.HexColor('#244f3d')
        flow = [Image(str(assets/'aerotrace.png'), width=44, height=44), Spacer(1, 12)]
        pdf_inline=lambda text: inline(text).replace('<code>','<font name="Courier">').replace('</code>','</font>')
        for block in content:
            if block[0]=='heading':
                style='Title' if block[1]==1 else 'Heading1' if block[1]==2 else 'Heading2'
                flow.append(Paragraph(pdf_inline(block[2]), styles[style]))
            elif block[0]=='table':
                cells=[[Paragraph(pdf_inline(c), styles['GuideHeaderCell' if r==0 else 'GuideCell']) for c in row] for r,row in enumerate(block[1])]
                table=Table(cells, colWidths=[160,363], repeatRows=1, hAlign='LEFT')
                table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#244f3d')),('GRID',(0,0),(-1,-1),.4,colors.HexColor('#dce1db')),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7)]))
                flow.extend([table, Spacer(1,12)])
            else:
                text=(block[2]+'. ' if block[0]=='list' else '')+block[1]
                flow.append(Paragraph(pdf_inline(text),styles['GuideBody']))
        def page(canvas, doc):
            canvas.saveState();canvas.setFont('Guide',8);canvas.setFillColor(colors.HexColor('#657066'));canvas.drawString(36,22,'AeroTrace 1553 · Operator guide · 0.6.0');canvas.drawRightString(A4[0]-36,22,str(doc.page));canvas.restoreState()
        SimpleDocTemplate(str(DOCS/'Operator-Guide.pdf'),pagesize=A4,rightMargin=36,leftMargin=36,topMargin=36,bottomMargin=36,title='AeroTrace 1553 Operator Guide',author='AeroTrace').build(flow,onFirstPage=page,onLaterPages=page)
    print('Built offline guide: '+str(DOCS/'Operator-Guide.html'))


if __name__=='__main__':
    main()
