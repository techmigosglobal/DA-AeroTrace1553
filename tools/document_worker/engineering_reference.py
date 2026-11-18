"""Small authored reference, explicitly separate from supplier ICD evidence."""
import re

REFERENCES = {
    "ieee754": ("IEEE 754 binary floating point",
        "IEEE 754 binary32 (float32) uses 32 bits: 1 sign bit, 8 exponent bits and 23 fraction bits. "
        "Binary64 (float64 / double) uses 64 bits: 1 sign bit, 11 exponent bits and 52 fraction bits. "
        "On a 16-bit bus these occupy 2 and 4 words respectively. Word order must come from the ICD or operator confirmation. "
        "The MSW-first raw words 3F80 0000 encode float32 1.0. This is bit interpretation, not an integer-to-float numeric cast. "
        "NaN and infinity do not become engineering measurements."),
    "radix": ("Raw word bases and signed integers",
        "Hexadecimal, decimal, binary and octal are different ways to write the same raw bits. A raw bus word is an unsigned 16-bit quantity from 0 to 65535. "
        "0xFFFF, decimal 65535, binary 0b1111111111111111 and octal 0o177777 represent the same word. "
        "An explicitly signed 16-bit two's-complement mapping interprets that word as -1. "
        "Select encoding, signedness, bit range, scale and offset independently of the input base."),
    "calibration": ("Native engineering conversion",
        "The validated native decoder selects the configured words and contiguous bit range, interprets the declared encoding, then computes engineering = decoded × scale + offset. "
        "A matching subsystem/message/channel/bus route and eligible message quality are required. Undocumented scale, offset, word order or bit numbering need explicit review. "
        "A finite decoded result does not establish physical plausibility. Limits and an independently known sample provide additional checks."),
    "bcd": ("Binary coded decimal",
        "BCD stores one decimal digit in each 4-bit nibble. Each digit must be from 0 to 9. A mapping's declared width and scale determine how its digits become an engineering value. "
        "The native decoder rejects invalid BCD digits; it does not reinterpret them as an unsigned binary integer."),
}


def answer(request):
    query = request.get("question", "").lower()
    if re.search(r"\b(preview|previewed)\b",query) and re.search(r"\b(explain|show|last|result)\b",query):
        preview = str(request.get("native_preview", ""))[:6000]
        if preview:
            return dict(answer=preview,mode="native-preview",citations=[dict(kind="native-preview",document="Last native conversion preview",page=0,hash="",excerpt=preview)])
    if re.search(r"\bid[_ -]?\d+\b",query):
        return None
    key = ""
    general = bool(re.search(r"\b(what is|what are|explain|difference|how does|how do)\b",query))
    if general and re.search(r"\b(ieee|binary32|binary64|floating.point)\b",query): key="ieee754"
    elif general and re.search(r"\b(hexadecimal|binary|octal|radix|two.s.complement)\b",query): key="radix"
    elif general and re.search(r"\b(bcd|binary coded decimal)\b",query): key="bcd"
    elif general and re.search(r"\b(engineering conversion|scale and offset|calibration formula)\b",query): key="calibration"
    if not key:
        return None
    title,text = REFERENCES[key]
    return dict(answer="Engineering reference — not a supplier ICD definition:\n"+text,mode="engineering-reference",
                citations=[dict(kind="engineering-reference",reference_id=key,document=title,page=0,hash="",excerpt=text)])
