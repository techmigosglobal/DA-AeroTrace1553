"""Offline PDF/OCR ICD ingestion and evidence-based retrieval. JSON-lines protocol.

One process per job; no hardware access, shell execution, cloud calls or eval.
Unsupported/ambiguous rows are evidence for review, never fabricated mappings.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sqlite3
import sys
import tempfile
import heapq
import urllib.request
from datetime import datetime, timezone

sys.path.insert(0, str(Path(__file__).resolve().parent))
import retrieval_context
from structured_answers import entries as signal_entries, index_rows, metadata as signal_metadata, query as structured_query

os.environ.setdefault("OMP_NUM_THREADS", "2")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
SCHEMA = "aerotrace.icd.profile.v2"
MAX_PAGES = 500
MAX_BYTES = 100 * 1024 * 1024
ALIASES = {
    "id": {"signal", "signalid", "signalname", "parameter", "parametername", "name", "mnemonic"},
    "rt": {"rt", "rtaddress", "remoteterminal"},
    "sa": {"sa", "subaddress", "subaddr"},
    "direction": {"direction", "tr", "transmitreceive", "transferdirection"},
    "word": {"word", "wordindex", "wordindex0based", "wordindexzerobased", "wordnumber", "wordnumber1based", "dataword"},
    "word_count": {"wordcount", "wordcountwc", "wc"},
    "lsb": {"lsb", "bitoffset", "startbit"},
    "width": {"width", "bitwidth", "bitlength", "bits", "bitrange"},
    "scale": {"scale", "resolution", "lsbvalue", "scalefactor"},
    "offset": {"offset", "bias"},
    "unit": {"unit", "units", "engineeringunit"},
    "type": {"type", "datatype", "numerictype", "format", "representation"},
    "signed": {"signed", "signedness"},
    "order": {"wordorder", "endianness"},
    "channel": {"channel", "ch"},
    "bus": {"bus"},
    "mask": {"mask", "bitmask"},
    "shift": {"shift", "rightshift"},
    "enumeration": {"enum", "enumeration", "states", "valuemeanings"},
    "expression": {"bitwise", "bitwiseexpression", "bitoperation", "decodeexpression"},
}


def emit(kind, **fields):
    print(json.dumps({"event": kind, **fields}, ensure_ascii=False, allow_nan=False), flush=True)


def normal(value):
    return re.sub(r"[^a-z0-9]", "", str(value or "").lower())


def number(value):
    text = str(value).strip().replace("−", "-").replace("×", "*")
    if re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", text):
        result = float(text)
    elif re.fullmatch(r"2\s*(?:\^|\*\*)\s*[+-]?\d+", text):
        exponent = int(re.split(r"\^|\*\*", text)[1])
        if not -1022 <= exponent <= 1023:
            raise ValueError("Scale exponent outside supported range")
        result = 2.0 ** exponent
    elif re.fullmatch(r"[+-]?\d+\s*/\s*\d+", text):
        numerator, denominator = text.split("/")
        result = int(numerator) / int(denominator)
    else:
        raise ValueError("Unsupported numeric expression: " + text)
    if not math.isfinite(result):
        raise ValueError("Non-finite numeric value")
    return result


def integer(value):
    parsed = number(value)
    if parsed != int(parsed):
        raise ValueError("Expected integer: " + str(value))
    return int(parsed)


def compact_message(rows, page, digest):
    aliases = {"messageid": "message_id", "rtaddress": "rt", "subaddr": "sa",
               "msgtype": "direction", "wordcount": "word_count"}
    values = {aliases[key]: row[i+1] for row in rows for i in range(0, len(row)-1, 2)
              if (key := normal(row[i])) in aliases}
    if len(values) != len(aliases):
        return None
    try:
        for key in ("rt", "sa", "word_count"):
            values[key] = integer(values[key])
    except (ValueError, OverflowError):
        return {}
    if not (0 <= values["rt"] <= 30 and 1 <= values["sa"] <= 30 and 1 <= values["word_count"] <= 32
            and normal(values["direction"]) in {"receive", "transmit"} and values["message_id"]):
        return {}
    values.update(direction=normal(values["direction"]), header_page=page, header_hash=digest,
                  header_text=" | ".join(cell for row in rows for cell in row if cell), compact=True)
    return values


def compact_fields(rows, columns, page, method, digest, state, group):
    def field(row, key):
        column = columns.get(key)
        return row[column] if column is not None and column < len(row) else ""
    spans = [re.fullmatch(r"(\d+)\s*(?:[-–—]\s*(\d+))?", field(row, "word")) for row in rows]
    if any(span and span[1] == "0" for span in spans):
        state["zero_based"] = True
    results = []
    for row, span in zip(rows, spans):
        name, datatype = field(row, "id"), field(row, "type")
        if not name:
            continue
        signal_id = " / ".join([*group[:1], state["message_id"], name])
        kind = normal(datatype)
        entry = {"id": signal_id, "page": page, "text": " | ".join(row), "method": method,
                 "source_hash": digest, "table_icd": True,
                 "subsystem": group[0] if group else "", "message_id": state["message_id"]}
        if not kind:
            entry["reason"] = "Data type is unspecified. Kept as document evidence."
            results.append(entry)
            continue
        cells = dict(id=signal_id, rt=state["rt"], sa=state["sa"], direction=state["direction"],
                     word_count=state["word_count"], unit=field(row, "unit"))
        entry.update(source_signal_id=name, subsystem=group[0] if group else "", message_id=state["message_id"])
        if span and state.get("zero_based"):
            start, end = int(span[1]), int(span[2] or span[1])
            word_format = re.fullmatch(r"(\d+)words?", normal(field(row, "format")))
            if word_format and 0 <= start <= end < state["word_count"] and end-start+1 == int(word_format[1]):
                cells["data_word"] = start+1
                entry["printed_words"] = span[0]
                encoding = {key: field(row, key) for key in ("lsb", "width", "signed", "order", "scale", "offset",
                                                             "mask", "shift", "enumeration", "expression")}
                encoding.update(type="uint16" if kind == "usi" else datatype, unit=cells["unit"])
                try:
                    fields = partial_table_fields(normalize_encoding(encoding), "Word number", "")
                    if fields.get("width", 0) + fields.get("lsb", 0) <= 16*(end-start+1):
                        cells.update(fields)
                except (ValueError, OverflowError, ZeroDivisionError) as error:
                    entry["reason"] = str(error)
        entry.update(draft_cells=cells, draft_field={"message": dict(state)})
        entry.setdefault("reason", "Confirm word order, conversion and stream for this table.")
        results.append(entry)
    return results


def parse_message_layout(tables, page, method, digest, state, group=(), continuation=False):
    """Extract separate message headers and word-range tables as review-only evidence.

    The observed word span is not an assertion of IEEE format, signed encoding,
    ordering, scale, name, or suitability for engineering conversion.
    """
    pending = []
    for table in tables:
        rows = [[str(cell or "").strip().replace("\n", " ") for cell in row] for row in table if row]
        if not rows:
            continue
        header = compact_message(rows, page, digest)
        if header is not None:
            state.clear()
            state.update(header)
            continue
        names = [normal(c) for c in rows[0]]
        def column(*aliases):
            return next((i for i, name in enumerate(names) if name in aliases), None)
        if state.get("compact"):
            columns = {"word": column("wordno", "wordnumber", "wordindex"), "id": column("signalid"),
                       "unit": column("unit", "units"), "format": column("wordformat"),
                       "type": column("datatype", "type")}
            if all(value is not None for value in columns.values()):
                for key in ("lsb", "width", "signed", "order", "scale", "offset", "mask", "shift", "enumeration", "expression"):
                    if (optional := column(*ALIASES[key])) is not None:
                        columns[key] = optional
                state["columns"] = columns
                pending.extend(compact_fields(rows[1:], columns, page, method, digest, state, group))
            elif continuation and state.get("columns") and all(len(row) > max(state["columns"].values()) for row in rows):
                pending.extend(compact_fields(rows, state["columns"], page, method, digest, state, group))
            continue
        rt, sa = column("rt", "remoteterminal", "remoteterminalrt", "rtaddress"), column("sa", "subaddress", "subaddresssa", "subaddr")
        wc, direction = column("wordcountwc", "wordcount", "wc"), column("type", "direction")
        mid = column("messageid", "messageidentifier")
        if all(i is not None for i in (rt, sa, wc, direction)):
            state.clear()  # never keep an earlier header if this one is malformed
            if len(rows) != 2 or max(rt, sa, wc, direction) >= len(rows[1]):
                continue
            row = rows[1]
            try:
                address, subaddress, count = integer(row[rt]), integer(row[sa]), integer(row[wc])
                d = normal(row[direction])
                if not (0 <= address <= 30 and 1 <= subaddress <= 30 and 1 <= count <= 32 and d in {"receive", "rx", "r", "transmit", "tx", "t"}):
                    continue
                state.update(rt=address, sa=subaddress, word_count=count,
                             direction="receive" if d in {"receive", "rx", "r"} else "transmit",
                             message_id=row[mid] if mid is not None and mid < len(row) else "",
                             header_page=page, header_hash=digest, header_text=" | ".join(row))
            except (ValueError, OverflowError):
                continue
            continue
        word, datatype, unit = column("words", "word", "wordnumber", "dataword"), column("datatype", "type"), column("unit", "units")
        if not state or any(i is None for i in (word, datatype, unit)):
            continue
        field_columns = {key: column(*aliases) for key, aliases in ALIASES.items()}
        signed_column = column("signed", "signedness")
        for row in rows[1:]:
            if max(word, datatype, unit) >= len(row):
                continue
            span = re.fullmatch(r"\s*(\d+)\s*(?:[-–—]\s*(\d+))?\s*", row[word])
            if not span:
                continue
            first, last = int(span[1]), int(span[2] or span[1])
            if not 1 <= first <= last <= state["word_count"]:
                continue
            if normal(row[datatype]) in {"reserved", "spare", "unused"}:
                continue
            explicit = {}
            for key in ("id", "lsb", "width", "scale", "offset", "order"):
                index = field_columns[key]
                if index is not None and index < len(row) and row[index]:
                    explicit[key] = row[index]
            if signed_column is not None and signed_column < len(row):
                signedness = normal(row[signed_column])
                if signedness in {"true", "yes", "signed", "false", "no", "unsigned"}:
                    explicit["signed"] = signedness in {"true", "yes", "signed"}
            kind = normal(row[datatype])
            representation = {
                "float32": ("float32", False), "ieee75432": ("float32", False),
                "float64": ("float64", False), "ieee75464": ("float64", False),
                "bcd": ("bcd", False), "packedbcd": ("bcd", False),
            }.get(kind)
            if re.fullmatch(r"u?int(?:8|16|32|64)", kind):
                representation = ("integer", kind.startswith("int"))
            if representation:
                explicit["representation"] = representation[0]
                if "signed" in explicit and explicit["signed"] != representation[1]:
                    explicit.pop("signed")
                else:
                    explicit["signed"] = representation[1]
            for key in ("lsb", "width", "scale", "offset"):
                if key not in explicit:
                    continue
                try:
                    explicit[key] = (integer if key in {"lsb", "width"} else number)(explicit[key])
                except (ValueError, ZeroDivisionError, OverflowError):
                    explicit.pop(key)
            encoding_row = {key: str(value) for key, value in explicit.items()}
            encoding_row.update(type=row[datatype], unit=row[unit])
            try:
                fields = partial_table_fields(normalize_encoding(encoding_row), "Word number", "")
                if fields.get("width", 0) + fields.get("lsb", 0) <= (last-first+1)*16:
                    for key in ("lsb", "width", "representation", "signed", "scale", "offset"):
                        if key in fields:
                            explicit[key] = fields[key]
                explicit["order"] = fields.get("word_order", "")
            except (ValueError, OverflowError):
                explicit["order"] = ""
            pending.append({"page": page, "id": explicit.get("id", ""), "text": " | ".join(row),
                "reason": "Draft only: confirm name, representation, word order, signedness, scale, offset and units before publication",
                "method": method, "source_hash": digest,
                "draft_field": {"message": dict(state), "word_first": first, "word_last": last,
                                "word_index": first-1, "word_span": last-first+1,
                                "document_type": row[datatype], "document_unit": row[unit], "explicit": explicit}})
    return pending


def word_index_base(label, context):
    header = normal(label)
    if "0based" in header or "zerobased" in header:
        return 0
    if "1based" in header or "onebased" in header or "number" in header:
        return 1
    hints = set(re.findall(r"word\s+(?:indices|indexes|indexing)[^.;\n]*?(zero|0|one|1)[ -]based", context, re.I))
    bases = {0 if hint.lower() in {"zero", "0"} else 1 for hint in hints}
    return bases.pop() if len(bases) == 1 else None


def explicit_signedness(value):
    token = normal(value)
    if token in {"true", "yes", "signed"}:
        return True
    if token in {"false", "no", "unsigned"}:
        return False
    return None


def normalize_encoding(row):
    """Derive only properties implied by a documented representation or field."""
    kind = normal(row.get("type"))
    float_width = {
        "float": 32, "single": 32, "float32": 32, "ieee75432": 32, "binary32": 32,
        "double": 64, "float64": 64, "ieee75464": 64, "binary64": 64,
    }.get(kind)
    integer_type = re.fullmatch(r"(u?int)(8|16|32|64)", kind)
    flag = kind in {"flag", "bit", "bool", "boolean", "signedbit", "unsignedbit"}
    enum = kind in {"enum", "enumeration", "bitfield"} or bool(row.get("enumeration"))
    width = float_width or (int(integer_type[2]) if integer_type else 1 if flag else None)
    if float_width:
        row["type"] = f"float{float_width}"
    if flag or enum:
        if kind in {"signedbit", "unsignedbit"}:
            row["type"] = "signed" if kind == "signedbit" else "unsigned"
        elif not row.get("signed"):
            row["type"] = "unsigned"
        elif explicit_signedness(row["signed"]) is not None:
            row["type"] = "signed" if explicit_signedness(row["signed"]) else "unsigned"
        row["scale"] = row.get("scale") or "1"
        row["offset"] = row.get("offset") or "0"
        row["unit"] = row.get("unit") or "unitless"
    if kind == "integer" and explicit_signedness(row.get("signed")) is not None:
        row["type"] = "signed" if explicit_signedness(row["signed"]) else "unsigned"
    if row.get("expression"):
        match = re.fullmatch(r"\(\s*(?:raw|word)\s*&\s*(0[xX][0-9a-fA-F]{1,16}|0[bB][01]{1,64})\s*\)\s*>>\s*(\d{1,2})",
                             row["expression"].strip(), re.I)
        if not match or row.get("mask") or row.get("shift"):
            raise ValueError("Bitwise expression requires review; use one contiguous mask and right shift")
        row["mask"], row["shift"] = match.groups()
    if row.get("mask") or row.get("shift"):
        mask_text = str(row.get("mask", "")).strip()
        if not re.fullmatch(r"0[xX][0-9a-fA-F]{1,16}|0[bB][01]{1,64}", mask_text):
            raise ValueError("Enter a contiguous hexadecimal or binary mask and its right shift")
        mask = int(mask_text, 0)
        if not mask:
            raise ValueError("Bit mask must select at least one bit")
        lsb = (mask & -mask).bit_length() - 1
        width = (mask >> lsb).bit_length()
        if (mask >> lsb) != (1 << width) - 1:
            raise ValueError("Non-contiguous mask requires manual review")
        if row.get("shift") and integer(row["shift"]) != lsb:
            raise ValueError("Right shift disagrees with the mask")
        if (row.get("lsb") and integer(row["lsb"]) != lsb) or (row.get("width") and integer(row["width"]) != width):
            raise ValueError("Bit offset or width disagrees with the mask")
        row["lsb"], row["width"] = str(lsb), str(width)
    elif width:
        row["width"] = row.get("width") or str(width)
        if float_width or (integer_type and width >= 16):
            row["lsb"] = row.get("lsb") or "0"
    if normal(row.get("scale")) in {"identity", "unscaled", "raw"}:
        row["scale"], row["offset"] = "1", row.get("offset") or "0"
    if row.get("lsb", "").isdigit() and row.get("width", "").isdigit():
        if int(row["lsb"]) + int(row["width"]) <= 16:
            row["order"] = row.get("order") or "msw-first"
    return row


def partial_table_fields(row, word_label, context):
    result = {key: row[key] for key in ("id", "unit") if row.get(key)}
    directions = {"r": "receive", "rx": "receive", "receive": "receive", "bctort": "receive", "0": "receive",
                  "t": "transmit", "tx": "transmit", "transmit": "transmit", "rttobc": "transmit", "1": "transmit"}
    if direction := directions.get(normal(row.get("direction"))):
        result["direction"] = direction
    for key in ("rt", "sa", "lsb", "width", "word_count", "channel", "scale", "offset"):
        if not row.get(key):
            continue
        try:
            result[key] = (number if key in {"scale", "offset"} else integer)(row[key])
        except (ValueError, ZeroDivisionError, OverflowError):
            pass
    if bits := re.fullmatch(r"\s*(\d+)\s*[:\-–]\s*(\d+)\s*", row.get("width", "")):
        high, low = map(int, bits.groups())
        if high >= low and ("lsb" not in result or result["lsb"] == low):
            result.update(lsb=low, width=high-low+1)
        else:
            result.pop("lsb", None)
    base = word_index_base(word_label, context)
    if base is not None:
        try:
            result["data_word"] = integer(row.get("word", "")) - base + 1
        except (ValueError, OverflowError):
            pass
    kind = normal(row.get("type"))
    if re.fullmatch(r"u?int(?:8|16|32|64)?|signed|unsigned", kind):
        result.update(representation="integer", signed=kind.startswith("int") or kind == "signed")
    elif kind in {"float32", "ieee75432", "float64", "ieee75464", "bcd", "packedbcd"}:
        result.update(representation={"ieee75432": "float32", "ieee75464": "float64",
                                      "packedbcd": "bcd"}.get(kind, kind), signed=False)
    if row.get("signed"):
        declared = explicit_signedness(row["signed"])
        if declared is None or ("signed" in result and declared != result["signed"]):
            result.pop("signed", None)
        else:
            result["signed"] = declared
    order = {"mswfirst": "msw-first", "lswfirst": "lsw-first"}.get(normal(row.get("order")))
    if order:
        result["word_order"] = order
    if row.get("bus", "").upper() in {"A", "B", "ANY"}:
        result["bus"] = row["bus"].upper()
    return result


def parse_table(rows, page, source, digest, context, confidence=1.0, table_state=None, group=None):
    """Only activate rows whose addressing, indexing and conversion are explicit."""
    mappings, pending = [], []
    columns, header = table_state.get("header", ({}, [])) if table_state is not None else ({}, [])
    inherited = bool(header)
    numbering = table_state.get("numbering") if inherited else None
    for row_number, cells in enumerate(rows):
        row_confidence = confidence[row_number] if isinstance(confidence, list) else confidence
        cells = [str(cell or "").strip().replace("\n", " ") for cell in cells]
        found = {}
        for index, cell in enumerate(cells):
            for key, names in ALIASES.items():
                if normal(cell) in names:
                    found[key] = index
        if {"rt", "sa", "word"}.issubset(found) and len(found) >= 4:
            columns, header = found, cells
            inherited = False
            numbering = word_index_base(header[columns["word"]], context)
            if table_state is not None:
                table_state.update(header=(columns, header), numbering=numbering)
            continue
        if not columns or not any(cells):
            continue
        if inherited and len(cells) != len(header):
            continue
        row = {key: cells[index] if index < len(cells) else "" for key, index in columns.items()}
        evidence = {"page": page, "text": " | ".join(cells), "method": source, "confidence": round(row_confidence, 4)}
        normalized = False
        try:
            normalize_encoding(row)
            normalized = True
            required = {"id", "rt", "sa", "direction", "word", "width", "scale", "offset", "unit", "type"}
            missing = {key for key in required if not row.get(key)}
            if missing:
                raise ValueError("Missing explicit columns: " + ", ".join(sorted(missing)))
            if not row["id"]:
                raise ValueError("Missing signal ID")
            direction = normal(row["direction"])
            if direction in {"r", "rx", "receive", "bctort", "0"}:
                transmit = False
            elif direction in {"t", "tx", "transmit", "rttobc", "1"}:
                transmit = True
            else:
                raise ValueError("Ambiguous transfer direction: " + row["direction"])
            base = numbering
            if base is None:
                raise ValueError("Word indexing base is not explicit (0-based index or 1-based word number)")
            bits = re.fullmatch(r"\s*(\d+)\s*[:\-–]\s*(\d+)\s*", row["width"])
            if bits:
                high, low = map(int, bits.groups())
                if high < low:
                    raise ValueError("MSB-first bit numbering needs an explicit conversion")
                lsb, width = low, high - low + 1
            else:
                width = integer(row["width"])
                if "lsb" not in row:
                    raise ValueError("Bit offset/LSB is missing")
                lsb = integer(row["lsb"])
            datatype = normal(row["type"])
            if re.fullmatch(r"(?:u?int|unsigned|signed)(?:8|16|32|64)?", datatype):
                numeric_type = "integer"
                signed = datatype.startswith("int") or datatype.startswith("signed")
            elif datatype in {"float", "float32", "ieee75432", "single"}:
                numeric_type, signed = "float32", False
            elif datatype in {"double", "float64", "ieee75464"}:
                numeric_type, signed = "float64", False
            elif datatype in {"bcd", "packedbcd"}:
                numeric_type, signed = "bcd", False
            else:
                raise ValueError("Unsupported numeric representation: " + row["type"])
            if row.get("signed") and explicit_signedness(row["signed"]) != signed:
                raise ValueError("Signedness is ambiguous or conflicts with the representation")
            order = normal(row.get("order", ""))
            if width + lsb > 16 and order not in {"mswfirst", "lswfirst", "bigendian", "littleendian"}:
                raise ValueError("Multiword value needs explicit word order")
            identity = " / ".join([*(group or []), row["id"]])
            mapping = {"schema": "aerotrace.icd.v1", "id": identity, "unit": row["unit"],
                       "rt": integer(row["rt"]), "sa": integer(row["sa"]), "word": integer(row["word"]) - base,
                       "lsb": lsb, "width": width, "expected_word_count": integer(row.get("word_count") or "0"), "scale": number(row["scale"]), "offset": number(row["offset"]),
                       "signed": signed, "transmit": transmit, "numeric_type": numeric_type,
                       "lsw_first": order in {"lswfirst", "littleendian"}, "channel": integer(row.get("channel", "0") or "0"),
                       "bus": {"A": 0, "B": 1, "": -1, "ANY": -1}.get(row.get("bus", "").upper(), -99),
                       "source_page": page, "source_hash": digest, "source_document": source,
                       "evidence": evidence}
            if not 0 <= mapping["rt"] <= 31 or not 0 <= mapping["sa"] <= 31:
                raise ValueError("RT/SA outside 0–31")
            if not 0 <= mapping["word"] < 32 or not 0 <= lsb < 16 or not 1 <= width <= 64 or width + lsb > 64 or mapping["word"] + (width + lsb + 15) // 16 > 32:
                raise ValueError("Bit field outside supported payload")
            if numeric_type.startswith("float") and (lsb or width != int(numeric_type[5:])):
                raise ValueError("Float width/alignment mismatch")
            if numeric_type == "bcd" and width % 4:
                raise ValueError("BCD width must be a multiple of four")
            if mapping["channel"] not in {0, 1, 2} or mapping["bus"] not in {-1, 0, 1}:
                raise ValueError("Invalid channel/bus")
            if row_confidence < .98:
                pending.append({**evidence, "id": mapping["id"], "reason": "OCR confidence below 0.98; verify this candidate against the page", "candidate": mapping})
            else:
                mappings.append(mapping)
        except (ValueError, ZeroDivisionError, OverflowError) as error:
            word_label = header[columns["word"]] if numbering is None else f"Word index ({numbering}-based)"
            fields = partial_table_fields(row, word_label, context)
            if fields.get("id"):
                fields["id"] = " / ".join([*(group or []), fields["id"]])
            if not normalized and (row.get("mask") or row.get("shift")):
                # Require explicit confirmation of the bit layout after any bitwise error.
                fields.pop("lsb", None)
                fields.pop("width", None)
            pending.append({**evidence, "id": row.get("id", ""), "reason": str(error),
                            "source_hash": digest, "draft_cells": fields})
    return mappings, pending


def database(store):
    store = Path(store).resolve()
    store.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(store / "documents.sqlite")
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("CREATE TABLE IF NOT EXISTS documents(hash TEXT PRIMARY KEY, name TEXT, pages INTEGER)")
    db.execute("CREATE TABLE IF NOT EXISTS document_profiles(hash TEXT PRIMARY KEY, profile_json TEXT NOT NULL)")
    db.execute("CREATE TABLE IF NOT EXISTS signal_entries(profile_hash TEXT, position INTEGER, subsystem TEXT, encoding TEXT, row_json TEXT, PRIMARY KEY(profile_hash,position))")
    db.execute("CREATE INDEX IF NOT EXISTS signal_scope ON signal_entries(profile_hash,subsystem,encoding)")
    db.execute("CREATE TABLE IF NOT EXISTS chat_results(message_id INTEGER PRIMARY KEY, result_json TEXT NOT NULL)")
    db.execute("CREATE TABLE IF NOT EXISTS icd_set_members(set_hash TEXT, document_hash TEXT, position INTEGER, PRIMARY KEY(set_hash,position))")
    retrieval_context.initialize(db)
    db.execute("CREATE VIRTUAL TABLE IF NOT EXISTS chunks USING fts5(hash UNINDEXED, page UNINDEXED, text)")
    db.execute("CREATE TABLE IF NOT EXISTS embeddings(chunk_id INTEGER PRIMARY KEY, hash TEXT, version TEXT, vector BLOB)")
    db.execute("CREATE INDEX IF NOT EXISTS embeddings_document ON embeddings(hash,version)")
    db.execute("CREATE TABLE IF NOT EXISTS chat_messages(id INTEGER PRIMARY KEY, hash TEXT, role TEXT, content TEXT, utc TEXT)")
    db.execute("CREATE INDEX IF NOT EXISTS chat_document ON chat_messages(hash,id)")
    from library_history import migrate
    migrate(db)
    return db


def group_ocr(result):
    lines = []
    for box, text, score in result or []:
        y = sum(point[1] for point in box) / 4
        height = max(point[1] for point in box) - min(point[1] for point in box)
        target = next((line for line in lines if abs(line["y"] - y) < max(5, height * .5)), None)
        if target is None:
            target = {"y": y, "cells": []}
            lines.append(target)
        target["cells"].append((min(point[0] for point in box), text, float(score)))
    ordered = [sorted(line["cells"]) for line in sorted(lines, key=lambda line: line["y"])]
    return [[cell[1] for cell in line] for line in ordered], min((cell[2] for line in ordered for cell in line), default=0)


def grid_ocr(image, engine):
    """Recognize table cells in batches, preserving empty/single-digit columns.

    Whole-page text detection commonly misses isolated address bits and zeros.
    Ruled tables provide explicit cell boundaries for the recognizer instead.
    """
    import cv2
    import numpy as np
    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    binary = cv2.threshold(gray, 190, 255, cv2.THRESH_BINARY_INV)[1]
    height, width = gray.shape
    horizontal = cv2.morphologyEx(binary, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (max(25, width // 25), 1)))
    vertical = cv2.morphologyEx(binary, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (1, max(25, height // 25))))
    contours, _ = cv2.findContours(horizontal | vertical, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    tables = []

    def centers(values):
        groups = []
        for value in values:
            if not groups or value > groups[-1][-1] + 2:
                groups.append([value])
            else:
                groups[-1].append(value)
        return [int(sum(group)/len(group)) for group in groups]

    for contour in sorted(contours, key=lambda item: cv2.boundingRect(item)[1]):
        x, y, w, h = cv2.boundingRect(contour)
        if w < 200 or h < 60:
            continue
        xs = centers(np.where(np.count_nonzero(vertical[y:y+h, x:x+w], axis=0) > h * .70)[0])
        ys = centers(np.where(np.count_nonzero(horizontal[y:y+h, x:x+w], axis=1) > w * .70)[0])
        if not 5 <= len(xs) <= 30 or not 3 <= len(ys) <= 200:
            continue
        crops, positions = [], []
        rows = [["" for _ in range(len(xs)-1)] for _ in range(len(ys)-1)]
        scores = [[1.0 for _ in range(len(xs)-1)] for _ in range(len(ys)-1)]
        for row in range(len(ys)-1):
            for col in range(len(xs)-1):
                crop = image[y+ys[row]+4:y+ys[row+1]-3, x+xs[col]+4:x+xs[col+1]-3]
                if not crop.size:
                    continue
                ink = cv2.threshold(cv2.cvtColor(crop, cv2.COLOR_RGB2GRAY), 190, 255, cv2.THRESH_BINARY_INV)[1]
                points = cv2.findNonZero(ink)
                if points is None:
                    continue
                left, top, cw, ch = cv2.boundingRect(points)
                if cw < 2 or ch < 3:
                    continue
                crop = crop[max(0, top-6):top+ch+6, max(0, left-6):left+cw+6]
                crop = cv2.copyMakeBorder(crop, 6, 6, 6, 6, cv2.BORDER_CONSTANT, value=(255,255,255))
                crops.append(crop)
                positions.append((row, col))
        if len(crops) > 4000:
            continue
        if crops:
            recognition, _ = engine.text_recognizer(crops)
            for (row, col), (text, score) in zip(positions, recognition):
                rows[row][col], scores[row][col] = text, float(score)
            tables.append((rows, [min(row) for row in scores]))
    return tables


def correct_ctc_confidence(engine):
    """RapidOCR 1.2.3 appends a near-zero sentinel when averaging confidence.

    It halves a single-character cell's score. Decode the same predictions with
    the standard CTC mask and mean over emitted characters only; no score boost
    or numeric OCR substitution is applied.
    """
    import numpy as np
    characters = engine.text_recognizer.postprocess_op.character

    class Decoder:
        def __call__(self, predictions):
            indexes = predictions.argmax(axis=2)
            probabilities = predictions.max(axis=2)
            results = []
            for ids, scores in zip(indexes, probabilities):
                keep = np.ones(len(ids), dtype=bool)
                keep[1:] = ids[1:] != ids[:-1]
                keep &= ids != 0
                text = "".join(characters[int(value)] for value in ids[keep])
                confidence = float(scores[keep].mean()) if keep.any() else 0.0
                results.append((text, confidence))
            return results
    engine.text_recognizer.postprocess_op = Decoder()


def extract_pages(path, digest, context):
    import pdfplumber
    import pypdfium2 as pdfium

    all_mappings, all_pending, pages = [], [], []
    ocr = None
    with pdfplumber.open(path) as pdf, pdfium.PdfDocument(path) as render:
        if not 1 <= len(pdf.pages) <= MAX_PAGES:
            raise ValueError("PDF must contain 1–500 pages")
        for index, page in enumerate(pdf.pages):
            text = (page.extract_text(layout=False) or "")[:500000]
            tables = page.extract_tables()
            method, confidence = "digital", 1.0
            if len(re.sub(r"\s", "", text)) < 30:
                if ocr is None:
                    from rapidocr_onnxruntime import RapidOCR
                    english_model = Path(__file__).parent / "models" / "en_PP-OCRv4_rec_mobile.onnx"
                    if not english_model.exists():
                        raise ValueError("English OCR model is missing; run scripts/setup-documents.ps1")
                    ocr = RapidOCR(rec_model_path=str(english_model), intra_op_num_threads=2, inter_op_num_threads=1)
                    correct_ctc_confidence(ocr)
                import numpy as np
                pdf_page = render[index]
                # Bound raster size rather than multiplying an arbitrarily large page.
                scale = min(3.0, 3000 / max(pdf_page.get_size()))
                bitmap = pdf_page.render(scale=scale)
                image = np.asarray(bitmap.to_pil().convert("RGB"))
                result, _ = ocr(image)
                rows, confidence = group_ocr(result)
                text = "\n".join(" | ".join(row) for row in rows)
                grids = grid_ocr(image, ocr)
                tables = [table for table, _ in grids] if grids else [rows]
                confidences = [scores for _, scores in grids] if grids else [confidence]
                if grids:
                    text += "\n" + "\n".join(" | ".join(row) for table, _ in grids for row in table)
                method = "ocr"
                bitmap.close()
                pdf_page.close()
            elif not tables:
                tables = [[re.split(r"\s*\|\s*|\t+|\s{2,}", line.strip().strip("|")) for line in text.splitlines()]]
                if not any(len(row) >= 4 for row in tables[0]):
                    tables = page.extract_tables({"vertical_strategy": "text", "horizontal_strategy": "text", "min_words_vertical": 2})
            page_context = text[:10000]
            continuation = bool(re.search(r"(?im)^.{0,100}\b(?:continued|continuation)\b.{0,25}$", page_context))
            subsystem = heading(page_context, "subsystem")
            if not subsystem:
                subsystem = list(dict.fromkeys("Subsystem-"+value for value in re.findall(
                    r"(?im)^[ \t]*sub(?:system|sytem)[ \t]*-[ \t]*([\w-]+)[ \t]*$", page_context)))
            message = heading(page_context, "message")
            ambiguous = len(subsystem) > 1 or len(message) > 1
            group = [*(subsystem[:1]), *(message[:1])]
            if continuation:
                previous_subsystem, previous_message = context.get("headings", ("", ""))
                subsystem = subsystem or ([previous_subsystem] if previous_subsystem else [])
                message = message or ([previous_message] if previous_message else [])
                group = [*subsystem[:1], *message[:1]]
            if ambiguous or (not continuation or group != context.get("group", [])):
                context["table"] = {}
            if ambiguous or group != context.get("group", []) or (not continuation and index == 0):
                context["layout"] = {}
            context["group"] = group if not ambiguous else []
            context["headings"] = (subsystem[0] if subsystem else "", message[0] if message else "") if not ambiguous else ("", "")
            page_mappings, page_pending = [], []
            page_pending.extend(parse_message_layout(tables, index + 1, method, digest,
                                                     context.setdefault("layout", {}), group, continuation))
            for table_index, table in enumerate(tables):
                table_confidence = confidences[table_index] if method == "ocr" else confidence
                table_state = context.setdefault("table", {})
                use_header = continuation and table_index == 0 and not ambiguous
                if not use_header:
                    table_state.clear()
                mappings, pending = parse_table(table, index + 1, method, digest, page_context,
                                                table_confidence, table_state, group if not ambiguous else [])
                if ambiguous:
                    for mapping in mappings:
                        draft = mapping_review(mapping, "Multiple subsystem or message headings. Enter a qualified signal ID.")
                        draft["draft_cells"].pop("id", None)
                        pending.append(draft)
                    mappings = []
                page_mappings.extend(mappings)
                page_pending.extend(pending)
            for mapping in page_mappings:
                mapping["source_document"] = path.name
                mapping["evidence"]["method"] = method
                if group:
                    mapping.update(subsystem=group[0], message_id=group[1] if len(group)>1 else "",
                                   source_signal_id=mapping["id"].split(" / ")[-1])
            for pending in page_pending:
                pending["source_document"] = path.name
                pending.setdefault("source_hash", digest)
                if pending.get("table_icd"):
                    continue
                if group and not ambiguous:
                    pending.update(subsystem=group[0], message_id=group[1] if len(group)>1 else "",
                                   source_signal_id=pending.get("id", pending.get("draft_cells",{}).get("id", "")).split(" / ")[-1])
                if pending.get("draft_field"):
                    explicit = pending["draft_field"]["explicit"]
                    if group and explicit.get("id") and not ambiguous:
                        explicit["id"] = " / ".join([*group, explicit["id"]])
                        pending["id"] = explicit["id"]
                    elif ambiguous:
                        explicit.pop("id", None)
                        pending["needs_identity"] = True
                        pending["reason"] = "Multiple subsystem or message headings. Enter a qualified signal ID."
                if "candidate" in pending:
                    pending["candidate"]["source_document"] = path.name
                    pending["candidate"]["evidence"]["method"] = method
            all_mappings.extend(page_mappings)
            all_pending.extend(page_pending)
            pages.append(text)
            emit("progress", page=index + 1, total=len(pdf.pages), method=method, mapped=len(all_mappings), review=len(all_pending))
    return pages, all_mappings, all_pending


def heading(text, label):
    values = re.findall(rf"(?im)^[ \t]*{label}(?:[ \t]+name)?[ \t]*:[ \t]*([^\r\n|]{{1,80}})", text)
    return list(dict.fromkeys(re.sub(r"(?i)\s*\(?(?:continued|continuation)\)?\s*$", "", value.strip())
                              for value in values if value.strip()))


def mapping_review(mapping, reason):
    evidence = mapping["evidence"]
    fields = {key: mapping[key] for key in ("id", "rt", "sa", "lsb", "width", "signed", "scale", "offset", "unit", "channel")}
    fields.update(word_count=mapping["expected_word_count"], data_word=mapping["word"] + 1,
                  direction="transmit" if mapping["transmit"] else "receive",
                  word_order="lsw-first" if mapping["lsw_first"] else "msw-first",
                  representation=mapping["numeric_type"], bus={-1: "any", 0: "A", 1: "B"}[mapping["bus"]])
    return {**evidence, "id": mapping["id"], "reason": reason, "source_hash": mapping["source_hash"],
            "source_document": mapping.get("source_document", ""), "draft_cells": fields,
            **{key: mapping.get(key, "") for key in ("subsystem","message_id","source_signal_id")}}


def validate_candidates(all_mappings, all_pending):
    by_id = {}
    for mapping in all_mappings:
        by_id.setdefault(mapping["id"].casefold(), []).append(mapping)
    unique = []
    def rule(mapping):
        return {k: v for k, v in mapping.items() if k not in {"evidence", "source_hash", "source_document", "source_page"}}
    for candidates in by_id.values():
        first = candidates[0]
        if any(rule(mapping) != rule(first) for mapping in candidates[1:]):
            all_pending.extend(mapping_review(mapping, "Conflicting definitions. Retain the correct row and remove the alternatives.")
                               for mapping in candidates)
        else:
            unique.append(first)
    if len(unique) > 512:
        raise ValueError("More than 512 signals; split the ICD into selected profiles")
    return unique


def index_import(db, store, documents, digest, name, is_set, profile):
    profile_json = json.dumps(profile, ensure_ascii=False, allow_nan=False)
    if len(profile_json.encode()) > 3 * 1024 * 1024:
        raise ValueError("ICD review exceeds 3 MB. Import the subsystems as smaller sets.")
    with db:
        from embedding import Encoder, VERSION
        encoder = Encoder()
        chunks = [(source, index + 1, text[start:start + 2000])
                  for _, source, pages in documents for index, text in enumerate(pages)
                  for start in range(0, len(text), 1800)]
        if len(chunks) > 20000:
            raise ValueError("ICD set exceeds 20000 searchable chunks")
        for path, source, pages in documents:
            destination = store / (source + ".pdf")
            if not destination.exists():
                temporary = destination.with_suffix(".pdf.tmp")
                shutil.copyfile(path, temporary)
                temporary.replace(destination)
            db.execute("DELETE FROM embeddings WHERE hash=?", (source,))
            db.execute("DELETE FROM chunks WHERE hash=?", (source,))
            db.execute("INSERT OR REPLACE INTO documents VALUES(?,?,?)", (source, path.name, len(pages)))
        if is_set:
            db.execute("INSERT OR REPLACE INTO documents VALUES(?,?,?)",
                       (digest, name, sum(len(pages) for _, _, pages in documents)))
            db.execute("DELETE FROM icd_set_members WHERE set_hash=?", (digest,))
            db.executemany("INSERT INTO icd_set_members VALUES(?,?,?)",
                           [(digest, source, index) for index, (_, source, _) in enumerate(documents)])
        db.execute("INSERT OR REPLACE INTO document_profiles VALUES(?,?)", (digest, profile_json))
        index_rows(db, digest, profile)
        retrieval_context.index(db, digest, signal_entries(profile))
        for start in range(0, len(chunks), 16):
            batch = chunks[start:start + 16]
            vectors = encoder.encode([text for _, _, text in batch])
            for (source, page, text), vector in zip(batch, vectors):
                cursor = db.execute("INSERT INTO chunks(hash,page,text) VALUES(?,?,?)", (source, page, text))
                db.execute("INSERT INTO embeddings VALUES(?,?,?,?)", (cursor.lastrowid, source, VERSION, vector.tobytes()))
            emit("progress", page=min(start + len(batch), len(chunks)), total=len(chunks), method="local embeddings")


def ingest(request):
    paths = request.get("pdfs") or [request["pdf"]]
    if not isinstance(paths, list) or not 1 <= len(paths) <= 64:
        raise ValueError("Select 1–64 PDFs per ICD set")
    set_name = str(request.get("set_name", "")).strip()
    if len(paths) > 1 and not set_name:
        raise ValueError("Name the ICD set")
    if len(set_name) > 120:
        raise ValueError("ICD set name exceeds 120 characters")
    documents, all_mappings, all_pending = [], [], []
    context, seen = {}, set()
    page_count, total_bytes = 0, 0
    for entry in paths:
        path = Path(entry).resolve(strict=True)
        total_bytes += path.stat().st_size
        if path.suffix.lower() != ".pdf" or total_bytes > MAX_BYTES:
            raise ValueError("Select PDFs totaling no more than 100 MB")
        with path.open("rb") as source_file:
            source = hashlib.file_digest(source_file, "sha256").hexdigest()
        if source in seen:
            continue
        seen.add(source)
        pages, mappings, pending = extract_pages(path, source, context)
        page_count += len(pages)
        if page_count > MAX_PAGES:
            raise ValueError("ICD set exceeds 500 pages")
        documents.append((path, source, pages))
        all_mappings.extend(mappings)
        all_pending.extend(pending)
    name = set_name or documents[0][0].name
    is_set = bool(set_name)
    digest = hashlib.sha256(json.dumps([name, [item[1] for item in documents]]).encode()).hexdigest() if is_set else documents[0][1]
    unique = validate_candidates(all_mappings, all_pending)
    profile = {"schema": SCHEMA, "document": name, "sha256": digest, "pages": page_count,
               "signals": unique, "review": all_pending, "automatic": False}
    store = Path(request["store"]).resolve()
    db = database(store)
    try:
        index_import(db, store, documents, digest, name, is_set, profile)
    finally:
        db.close()
    result = library({"store": str(store), "document_hash": digest})
    result.pop("library", None)
    return result


def chat_history(db, digest, conversation_id=None):
    from library_history import history
    return history(db,digest,conversation_id)


def document_scope(db, request):
    digest = str(request.get("document_hash", ""))
    if digest and (not re.fullmatch(r"[0-9a-f]{64}", digest) or not db.execute("SELECT 1 FROM documents WHERE hash=?", (digest,)).fetchone()):
        db.close()
        raise ValueError("Selected ICD is not indexed. Import the PDF first.")
    return digest


def stored_profile(db, store, digest):
    if not digest:
        return {}
    result = db.execute("SELECT profile_json FROM document_profiles WHERE hash=?", (digest,)).fetchone()
    if result:
        return json.loads(result[0])
    path = Path(store) / (digest + ".profile.json")
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}


def library(request):
    db = database(request["store"])
    store = Path(request["store"])
    from library_history import state, conversations
    documents = [dict(hash=digest, name=state(db,digest)["display_name"] or name, pages=pages, archived=state(db,digest)["archived"],icd_set_id=state(db,digest)["set_id"]) for digest, name, pages, indexed in db.execute(
        "SELECT d.hash,d.name,d.pages,p.hash IS NOT NULL FROM documents d LEFT JOIN document_profiles p ON d.hash=p.hash ORDER BY d.rowid DESC LIMIT 500")
                 if (indexed or (store / (digest + ".profile.json")).is_file()) and (request.get("include_archived") or not state(db,digest)["archived"])]
    digest = document_scope(db, request)
    if not digest and any(item["hash"]==request.get("preferred_hash") for item in documents):
        digest = request["preferred_hash"]
    if not digest and documents:
        digest = documents[0]["hash"]
    saved_history=chat_history(db,digest)
    result = dict(documents=documents, document_hash=digest, history=saved_history, library=True, conversation_id="legacy-"+digest if saved_history else "", conversations=conversations(db), archived=state(db,digest)["archived"])
    if saved_history:
        result.update(subsystem="",answer_source="documents")
    if digest:
        profile = stored_profile(db, store, digest)
        if profile:
            ownership=state(db,digest)
            profile["icd_set_id"]=ownership["set_id"];profile["icd_revision"]=digest
            for collection in ("signals","review"):
                for row in profile.get(collection,[]):
                    row["icd_set_id"]=ownership["set_id"];row["icd_revision"]=digest
            result["profile"] = profile
        first = db.execute("SELECT document_hash FROM icd_set_members WHERE set_hash=? ORDER BY position LIMIT 1", (digest,)).fetchone()
        result["pdf_path"] = str(store/((first[0] if first else digest)+".pdf"))
    owners={}
    for profile_hash,profile_json in db.execute("SELECT hash,profile_json FROM document_profiles"):
        owner=state(db,profile_hash)["set_id"]
        for row in signal_entries(json.loads(profile_json)):
            source=row.get("source_hash")
            if source: owners.setdefault(source,set()).add(owner)
    result["source_owners"]={key:sorted(value) for key,value in owners.items()}
    db.close()
    return result


def remember_answer(request, digest, question, result):
    if result.get("citations"):
        references = "\n".join(f'[{i+1}] {item["document"]}'+(f', page {item["page"]}' if item.get("hash") else " (local reference / native preview)") for i, item in enumerate(result["citations"]))
        result["answer"] = result["answer"]+"\n\nSources:\n"+references
    if not digest and not request.get("conversation_id"):
        return result
    db = database(request["store"])
    from library_history import resolve, conversations, now
    cid = resolve(db,digest,request.get("conversation_id"))
    with db:
        utc = datetime.now(timezone.utc).isoformat()
        db.execute("INSERT INTO chat_messages(hash,role,content,utc,conversation_id) VALUES(?,?,?,?,?)", (digest,"user",question[:2000],utc,cid))
        saved=db.execute("INSERT INTO chat_messages(hash,role,content,utc,conversation_id) VALUES(?,?,?,?,?)", (digest,"assistant",result["answer"][:12000],utc,cid))
        details={key: result[key] for key in ("table","citations","query","mode","events") if key in result}
        db.execute("INSERT INTO chat_results VALUES(?,?)",(saved.lastrowid,json.dumps(details,ensure_ascii=False,allow_nan=False)))
    db.execute("UPDATE conversations SET updated_utc=?, title=CASE WHEN title IN ('New conversation','ICD conversation') THEN ? ELSE title END WHERE id=?",(now(),question[:70],cid))
    db.commit()
    scope,source=db.execute("SELECT subsystem,answer_source FROM conversations WHERE id=?",(cid,)).fetchone()
    result.update(document_hash=digest, history=chat_history(db,digest,cid),conversation_id=cid,conversations=conversations(db),subsystem=scope,answer_source=source)
    db.close()
    return result


def signal_subjects(signals, query):
    def mentioned(text):
        parts = re.split(r"[\s/_-]+", text)
        pattern = r"[\s/_-]+".join(map(re.escape, parts))
        return bool(re.search(r"(?<!\w)" + pattern + r"(?!\w)", query, re.I))
    exact = [signal for signal in signals if mentioned(signal["id"])]
    names = [signal for signal in signals if mentioned(signal["id"].split(" / ")[-1])]
    matches = exact if any(" / " in signal["id"] for signal in exact) else names
    qualifiers = [(key, re.search(rf"\b{key}\s*[:=]?\s*(\d+)\b", query, re.I)) for key in ("rt", "sa")]
    matches = [signal for signal in matches if all(not match or
               signal.get(key, signal.get("draft_cells", {}).get(key)) == int(match[1]) for key, match in qualifiers)]
    scores = [sum(mentioned(group) for group in signal["id"].split(" / ")[:-1]) for signal in matches]
    best = max(scores, default=0)
    return [signal for signal, score in zip(matches, scores) if score == best]


def mapping_text(signal):
    direction = "RT-to-BC" if signal["transmit"] else "BC-to-RT"
    kind = signal.get("numeric_type", "integer")
    encoding = ("signed " if signal["signed"] else "unsigned ") + "integer" if kind == "integer" else kind
    order = "LSW first" if signal.get("lsw_first") else "MSW first"
    channel = f'CH{signal["channel"]}' if signal.get("channel") else "any channel"
    bus = {0: "bus A", 1: "bus B"}.get(signal.get("bus"), "either bus")
    count = f'{signal["expected_word_count"]} data words' if signal.get("expected_word_count") else "any payload length"
    return (f'RT {signal["rt"]} / SA {signal["sa"]}, {direction}; word {signal["word"]} (zero-based), '
            f'LSB {signal["lsb"]}, width {signal["width"]}, {encoding}, {order}. '
            f'{channel}, {bus}, {count}. '
            f'Engineering = extracted value * {signal["scale"]} + {signal["offset"]} {signal["unit"]}.')


def mapping_rule(signal):
    keys = ("rt", "sa", "transmit", "word", "lsb", "width", "numeric_type", "signed", "lsw_first",
            "scale", "offset", "unit", "expected_word_count", "channel", "bus")
    return tuple(signal.get(key) for key in keys)


def mapping_answer(request, digest, query, history, profile):
    published = request.get("answer_source") == "published"
    extracted = profile.get("signals", [])
    signals = request.get("active_profile", {}).get("signals", []) if published else extracted
    selected = request.get("subsystem", "")
    explicit = re.search(r"\bsubsystem[ -]*(\d+)\b", query, re.I)
    if explicit:
        selected = "Subsystem-"+explicit[1]
    if selected:
        from structured_answers import metadata
        signals = [row for row in signals if metadata(row).get("subsystem", "Unassigned") == selected]
    def subjects(text):
        return signal_subjects(signals, text)
    matches = subjects(query)
    if not matches and re.search(r"\b(it|its|this|that|same|previous|above|scale|offset|unit|formula|word|encoding)\b", query, re.I):
        for turn in reversed(history):
            if turn["role"] == "user" and (matches := subjects(turn["content"])):
                break
    if not matches or len(matches)>8:
        return None
    labels = [signal["id"].split(" / ")[-1].casefold() for signal in matches]
    if len(set(labels)) != len(labels):
        choices = "\n".join(f'- {signal["id"]} (RT {signal["rt"]}, SA {signal["sa"]})' for signal in matches)
        return dict(answer="Which subsystem or message did you mean?\n"+choices, citations=[], mode="clarification")
    if len(query.split())>3 and not re.search(r"\b(unit|scale|offset|word|address|rt|sa|bit|width|formula|convert|conversion|encoding|type|describe|mapping)\b", query, re.I):
        return None
    citations, lines = [], []
    for signal in matches:
        original = next((item for item in extracted if item["id"] == signal["id"]
                         and item.get("source_hash") == signal.get("source_hash")), None) if published else signal
        draft = next((item for item in profile.get("review", []) if item.get("id") == signal["id"]
                      and item.get("source_hash") == signal.get("source_hash")), None) if published and not original else None
        reference = ""
        if original:
            citations.append(dict(document=original.get("source_document") or profile["document"],
                                  hash=original.get("source_hash") or digest,page=original["source_page"],
                                  excerpt=original.get("evidence",{}).get("text",signal["id"]),rank=1))
            reference = f'[{len(citations)}]'
        elif draft:
            citations.append(dict(document=draft.get("source_document") or profile["document"],
                                  hash=draft["source_hash"], page=draft["page"], excerpt=draft["text"], rank=1))
            reference = f'[{len(citations)}]'
        if re.search(r"\bunit\b", query, re.I):
            lines.append(f'{signal["id"]}: {signal["unit"]}. {reference}')
        elif re.search(r"\bscale\b", query, re.I) and not re.search(r"\b(formula|convert|conversion)\b", query, re.I):
            lines.append(f'{signal["id"]}: scale {signal["scale"]}, offset {signal["offset"]}. {reference}')
        else:
            lines.append(f'{signal["id"]}: {mapping_text(signal)} {reference}')
        if published and original and mapping_rule(original) != mapping_rule(signal):
            lines[-1] = lines[-1].removesuffix(reference).rstrip() + "\nICD document " + reference + ": " + mapping_text(original)
        elif draft:
            lines[-1] = lines[-1].removesuffix(reference).rstrip() + "\nICD document " + reference + ": " + draft["text"]
        elif published and not original:
            lines[-1] += "\nNo matching PDF evidence is indexed for this published rule."
    title = "Published mappings (Engineering)" if published else "ICD documents"
    return dict(answer=title+"\n"+"\n\n".join(lines),citations=citations,
                mode="published-mapping" if published else "validated-mapping",retrieval=title)


def draft_layout_answer(profile, digest, query, history):
    """Answer layout questions using cited PDF fields without inventing conversion rules."""
    review = profile.get("review", [])
    compact = [row for row in review if row.get("table_icd")]
    subjects = signal_subjects(compact, query)
    if len(subjects) == 1:
        row = subjects[0]
        fields = row.get("draft_cells", {})
        layout = row.get("draft_field", {}).get("message", {})
        answer = f'{row["id"]}: {row["text"]}. [1]'
        if layout:
            answer += (f' RT {layout["rt"]} / SA {layout["sa"]}, {layout["direction"]}, '
                       f'{layout["word_count"]} data words. [1]')
        if fields:
            answer += " Open Published mappings for the engineering rule after review."
        return dict(answer=answer, mode="draft-layout", citations=[dict(document=row.get("source_document") or profile["document"],
                    hash=row["source_hash"], page=row["page"], excerpt=row["text"], rank=1)])
    if len(subjects) > 1:
        return dict(answer="Which subsystem or message did you mean?\n"+"\n".join(row["id"] for row in subjects[:8]),
                    mode="clarification", citations=[])
    drafts = [row for row in review if row.get("draft_field") and not row.get("table_icd")]
    if not drafts:
        return None
    word = re.search(r"\b(?:data )?word\s+(\d+)\b", query, re.I)
    if not word and re.search(r"\b(its|this word|that word|same word)\b", query, re.I):
        for turn in reversed(history):
            if turn["role"] == "user":
                word = re.search(r"\b(?:data )?word\s+(\d+)\b", turn["content"], re.I)
                if word:
                    break
    if word:
        number = int(word[1])
        matches = [row for row in drafts if row["draft_field"]["word_first"] <= number <= row["draft_field"]["word_last"]]
        if len(matches) != 1:
            return None
        row = matches[0]
        field = row["draft_field"]
        first, last = field["word_first"], field["word_last"]
        words = str(first) if first == last else f"{first}–{last}"
        answer = f'Data word range {words}: {field["document_type"]}, unit as printed: {field["document_unit"] or "unspecified"}. [1]'
        answer += " This row is draft evidence. Confirm representation, word order, signedness, scaling and units before publishing."
        page, excerpt = row["page"], row["text"]
        citation_hash = row.get("source_hash") or digest
    elif re.search(r"\b(rt|remote terminal|subaddress|word count|how many (?:data )?words|direction|message layout)\b", query, re.I):
        layouts = {json.dumps(row["draft_field"]["message"], sort_keys=True) for row in drafts}
        if len(layouts) != 1:
            return None
        message = json.loads(layouts.pop())
        page, excerpt = message["header_page"], message["header_text"]
        citation_hash = message.get("header_hash") or digest
        answer = (f'Message {message["message_id"]}: RT {message["rt"]}, subaddress {message["sa"]}, '
                  f'{message["direction"]}, {message["word_count"]} data words. [1]')
    else:
        return None
    source_name = next((row.get("source_document") for row in drafts if row.get("source_hash") == citation_hash), None)
    return dict(answer=answer, mode="draft-layout",
                citations=[dict(document=source_name or profile["document"], hash=citation_hash, page=page, excerpt=excerpt, rank=1)])


def ask(request):
    query = str(request.get("question", "")).strip()[:2000]
    terms = re.findall(r"[\w]+", query, re.UNICODE)[:32]
    if not terms:
        return {"answer": "Enter a question or signal name.", "citations": []}
    db = database(request["store"])
    digest = document_scope(db,request)
    if request.get("conversation_id") == "":
        from library_history import operate
        request["conversation_id"]=operate(db,dict(request,op="conversation_new",document_hash=digest))["conversation_id"]
    history = chat_history(db,digest,request.get("conversation_id")) if digest or request.get("conversation_id") else []
    profile = stored_profile(db, request["store"], digest)
    selected = request.get("subsystem", "")
    explicit = re.search(r"\bsubsystem[ -]*(\d+)\b", query, re.I)
    if explicit:
        selected = "Subsystem-"+explicit[1]
    if re.search(r"\b(all subsystems|all pages|entire icd)\b",query,re.I):
        selected=""
    visible_profile = profile.copy()
    if selected:
        from structured_answers import metadata
        for key in ("signals", "review"):
            visible_profile[key] = [row for row in profile.get(key, []) if metadata(row).get("subsystem", "Unassigned") == selected]
    if request.get("answer_source")=="published" and profile.get("icd_set_id"):
        selected_set=profile["icd_set_id"]
        source_hashes={row.get("source_hash") for row in signal_entries(profile)}
        active=dict(request.get("active_profile",{}))
        active["signals"]=[row for row in active.get("signals",[]) if row.get("icd_set_id")==selected_set or (not row.get("icd_set_id") and row.get("source_hash") in source_hashes)]
        request=dict(request,active_profile=active)
    from engineering_reference import answer as reference_answer
    reference=reference_answer(request)
    if reference:
        db.close()
        return remember_answer(request,digest,query,reference)
    direct = structured_query(request,profile,history,db) or mapping_answer(dict(request,subsystem=selected),digest,query,history,visible_profile)
    if request.get("answer_source") == "published":
        db.close()
        return remember_answer(request,digest,query,direct or {
            "answer": "No matching published mapping. Enter the signal ID or switch to ICD documents.",
            "citations": [], "mode": "published-mapping"})
    direct = direct or draft_layout_answer(visible_profile,digest,query,history)
    if direct:
        db.close()
        return remember_answer(request,digest,query,direct)
    retrieval_query = retrieval_context.followup(query, history)
    retrieval_terms = list(dict.fromkeys(terms + [term for term in re.findall(r"[A-Za-z0-9_]+", retrieval_query) if len(term) > 2]))[:30]
    expression = " OR ".join('"' + term.replace('"', '""') + '"' for term in retrieval_terms)
    scope = " AND (hash=? OR hash IN (SELECT document_hash FROM icd_set_members WHERE set_hash=?))" if digest else ""
    scoped_args = (digest, digest) if digest else ()
    subset = ""
    if selected:
        fields = signal_entries(profile)
        all_pages = {(row.get("source_hash"),row.get("source_page")) for row in fields}
        selected_pages = {(row.get("source_hash"),row.get("source_page")) for row in fields if row.get("subsystem")==selected}
        db.execute("CREATE TEMP TABLE scoped_chunks(id INTEGER PRIMARY KEY)")
        candidates=db.execute("SELECT rowid,hash,page FROM chunks WHERE 1=1"+scope,scoped_args).fetchall()
        db.executemany("INSERT INTO scoped_chunks VALUES(?)",[(identifier,) for identifier,source,page in candidates
                       if (source,page) in selected_pages or (source,page) not in all_pages])
        subset=" AND rowid IN (SELECT id FROM scoped_chunks)"
    lexical = db.execute("SELECT rowid FROM chunks WHERE chunks MATCH ?"+scope+subset+" ORDER BY bm25(chunks) LIMIT 20", (expression,*scoped_args)).fetchall()
    from embedding import Encoder, VERSION
    import numpy as np
    encoder = Encoder()
    query_vector = encoder.encode([retrieval_query])[0]
    nearest = []
    vector_subset=" AND chunk_id IN (SELECT id FROM scoped_chunks)" if selected else ""
    cursor = db.execute("SELECT chunk_id, vector FROM embeddings WHERE version=?"+scope+vector_subset+" LIMIT 100000", (VERSION,*scoped_args))
    while True:
        batch = cursor.fetchmany(512)
        if not batch:
            break
        matrix = np.stack([np.frombuffer(blob, dtype="<f4") for _, blob in batch])
        for (chunk_id, _), score in zip(batch, matrix @ query_vector):
            if score < .25:
                continue
            entry = (float(score), chunk_id)
            if len(nearest) < 20:
                heapq.heappush(nearest, entry)
            elif entry > nearest[0]:
                heapq.heapreplace(nearest, entry)
    ranking = {}
    for rank, (chunk_id,) in enumerate(lexical):
        ranking[chunk_id] = ranking.get(chunk_id, 0) + 1/(60+rank)
    for rank, (_, chunk_id) in enumerate(sorted(nearest, reverse=True)):
        ranking[chunk_id] = ranking.get(chunk_id, 0) + 1/(60+rank)
    rows = []
    for chunk_id, score in sorted(ranking.items(), key=lambda item: item[1], reverse=True)[:5]:
        row = db.execute("SELECT hash,page,text FROM chunks WHERE rowid=?", (chunk_id,)).fetchone()
        if row:
            rows.append((*row, score))
    citations = []
    indexed_fields=signal_entries(profile)
    for document_hash, page, text, rank in rows:
        name = db.execute("SELECT name FROM documents WHERE hash=?", (document_hash,)).fetchone()[0]
        headings=list(dict.fromkeys(" / ".join(filter(None,(row.get("subsystem"),row.get("message_id"))))
            for row in indexed_fields if row.get("source_hash")==document_hash and row.get("source_page")==page))
        citations.append({"document": name, "hash": document_hash, "page": int(page), "excerpt": text[:2000],
                          "context": " · ".join(filter(None,headings))[:400], "rank": rank})
    table_citations = []
    for source, page, excerpt, group, message, signal in retrieval_context.lookup(db, digest, indexed_fields, expression, selected, retrieval_query):
        name_row = db.execute("SELECT name FROM documents WHERE hash=?", (source,)).fetchone()
        if not name_row:
            continue
        table_citations.append(dict(document=name_row[0], hash=source, page=int(page),
            excerpt=excerpt, context=" / ".join(filter(None, (group, message, signal))), rank=1))
    if table_citations:
        # Prefer exact table evidence while keeping room for prose from another page.
        picked, seen = [], set()
        for candidate in table_citations[:2] + citations + table_citations[2:]:
            key = candidate["hash"], candidate["page"], candidate["excerpt"]
            if key not in seen:
                picked.append(candidate)
                seen.add(key)
            if len(picked) == 5:
                break
        citations = picked
    db.close()
    if not citations:
        return remember_answer(request,digest,query,{"answer": "No supporting passage found in the selected ICD." if digest else "No supporting passage found in the imported ICDs. No conversion rule was inferred.", "citations": [], "mode": "extractive"})
    # Deterministic extractive answer: the retrieved evidence is the answer.
    # A local language model can synthesize prose when explicitly configured.
    # Keep complete evidence in citations, while displaying only compact relevant sentences.
    snippets = []
    for index, item in enumerate(citations[:3]):
        sentences = re.split(r"(?<=[.!?])\s+|\n", item["excerpt"])
        ranked = sorted(enumerate(sentences), key=lambda pair: (-sum(term.lower() in pair[1].lower() for term in terms), pair[0]))
        excerpt = next((sentence.strip() for _, sentence in ranked if sentence.strip()), item["excerpt"])
        snippets.append(f'[{index+1}] {excerpt[:240]}'+("…" if len(excerpt)>240 else ""))
    answer = "\n".join(snippets)
    mode = "extractive"
    prepared = {"answer": answer, "citations": citations, "mode": mode, "retrieval": "Table evidence + BM25 + local MiniLM"}
    if request.get("prepare_generation"):
        return {**prepared, "needs_generation": True, "document_hash": digest, "question": query, "history": history[-4:],"conversation_id":request.get("conversation_id","")}
    return remember_answer(request,digest,query,prepared)


def complete_generation(request):
    prepared = request["prepared"]
    result = prepared.copy()
    result.pop("needs_generation", None)
    if request.get("local_model_url"):
        try:
            from local_generation import explain
            events=[]
            def stream(event, **data):
                events.append(dict(event=event, **data))
                emit(event, **data)
            result = explain(request, stream)
            result["events"]=events
            result.pop("needs_generation", None)
        except Exception as error:
            result["generation_note"] = "Model generation unavailable: "+str(error)+". Showing retrieved evidence."
    elif request.get("generation_note"):
        result["generation_note"] = request["generation_note"]
    if result.get("generation_note"):
        result["events"]=[dict(event="generation_unavailable",message=result["generation_note"])]
        result["answer"] += "\n\n"+result["generation_note"]
    return remember_answer(request,prepared.get("document_hash",""),request["question"],result)



def main():
    # Bounded input and one job per child process. stdout is protocol-only.
    raw = sys.stdin.buffer.readline(1024 * 1024 + 1)
    if len(raw) > 1024 * 1024:
        raise ValueError("Request exceeds 1 MB")
    request = json.loads(raw)
    operation = request.get("op")
    if operation == "ingest":
        result = ingest(request)
    elif operation == "ask":
        result = ask(request)
    elif operation == "explain":
        result = complete_generation(request)
    elif operation == "propose":
        from review_actions import propose
        db = database(request["store"])
        digest = document_scope(db, request)
        db.close()
        if not digest:
            raise ValueError("Select an indexed ICD first")
        result = {"proposal": propose(str(request.get("question", ""))[:2000], request.get("active_profile", {}), digest), "document_hash": digest}
    elif operation == "library":
        result = library(request)
    elif operation in {"conversations","conversation_new","conversation_load","library_rename","library_archive","library_restore","library_revision"}:
        from library_history import operate
        db=database(request["store"])
        try:
            result=operate(db,request)
            if operation=="conversation_load" and result.get("document_hash"):
                context=library(dict(request,document_hash=result["document_hash"]))
                context.update(result);result=context
        finally:
            db.close()
    elif operation == "health":
        import pypdfium2
        import rapidocr_onnxruntime
        from embedding import Encoder
        shape = Encoder().encode(["ICD engineering conversion"]).shape
        result = {"pdf": True, "ocr": True, "embedding_dimensions": int(shape[1]), "network_required": False}
    else:
        raise ValueError("Unsupported document operation")
    emit("done", **result)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        import traceback
        traceback.print_exc(file=sys.stderr)
        emit("error", message=str(error))
        sys.exit(1)
