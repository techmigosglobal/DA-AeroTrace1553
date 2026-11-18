"""Complete, source-scoped ICD table queries. Generation never supplies field values."""
import json
import re


def metadata(signal):
    result = dict(signal)
    parts = result.get("id", "").split(" / ")
    if len(parts) == 3 and parts[0].startswith("Subsystem-"):
        result["subsystem"] = result.get("subsystem") or parts[0]
        result["message_id"] = result.get("message_id") or parts[1]
        result["source_signal_id"] = result.get("source_signal_id") or parts[2]
    result.setdefault("source_signal_id", result.get("id", ""))
    return result


def entries(profile):
    rows = []
    for item in profile.get("signals", []):
        signal = metadata(item)
        row = dict(signal, review=False)
        row["word_first"] = signal["word"]
        row["word_last"] = signal["word"] + (signal["lsb"] + signal["width"] + 15) // 16 - 1
        row["encoding"] = signal.get("numeric_type", "integer")
        rows.append(row)
    for item in profile.get("review", []):
        fields = item.get("draft_cells", {})
        layout = item.get("draft_field", {})
        explicit = layout.get("explicit", {})
        signal = metadata(dict(item, id=fields.get("id") or item.get("id") or explicit.get("id", "")))
        signal.update(fields)
        signal = metadata(signal)
        signal["review"] = True
        signal["encoding"] = fields.get("representation") or explicit.get("representation", "")
        signal["signed"] = fields.get("signed", explicit.get("signed"))
        signal["unit"] = fields.get("unit", layout.get("document_unit", ""))
        signal["word_first"] = fields.get("data_word", layout.get("word_first", 0)) - 1
        signal["word_last"] = signal["word_first"] + (fields.get("lsb", 0) + fields.get("width", explicit.get("width", 16)) + 15) // 16 - 1
        signal["source_page"] = item.get("page", 0)
        signal["evidence"] = {"text": item.get("text", "")}
        rows.append(signal)
    return rows


def index_rows(db, digest, profile):
    db.execute("DELETE FROM signal_entries WHERE profile_hash=?", (digest,))
    db.executemany("INSERT INTO signal_entries(profile_hash,position,subsystem,encoding,row_json) VALUES(?,?,?,?,?)",
        [(digest, index, row.get("subsystem", ""), row["encoding"], json.dumps(row, ensure_ascii=False, allow_nan=False))
         for index, row in enumerate(entries(profile))])


def query(request, profile, history, db):
    text = request.get("question", "").strip()
    lower = text.lower()
    followup = bool(re.search(r"\b(all subsystems|all pages|their units|those fields|these signals|what about)\b", lower))
    previous = next((turn.get("result", {}).get("query", {}) for turn in reversed(history)
                     if turn.get("role") == "assistant" and turn.get("result", {}).get("query")), {})
    kind = ""
    if re.search(r"\b(float64|double|binary64)\b", lower):
        kind = "float64"
    elif re.search(r"\b(float32|single|binary32)\b", lower):
        kind = "float32"
    elif re.search(r"\b(float|floats|floating)\b", lower):
        kind = "float"
    elif re.search(r"\b(unsigned|usi)\b", lower):
        kind = "unsigned"
    elif re.search(r"\bsigned\b", lower):
        kind = "signed"
    elif re.search(r"\b(integer|integers)\b", lower):
        kind = "integer"
    elif re.search(r"\b(unresolved|incomplete|missing fields)\b", lower):
        kind = "unresolved"
    elif re.search(r"\b(list|show|how many|which|all)\b.*\b(signals|fields|words|mappings)\b", lower):
        kind = "all"
    elif followup:
        kind = previous.get("kind", "")
    if not kind:
        return None
    # Individual signal questions use the exact mapping/evidence answer path.
    if re.search(r"\bid[_ -]?\d+\b", lower) and not re.search(r"\b(list|which|how many)\b", lower):
        return None
    subsystem = request.get("subsystem", "")
    if followup and previous:
        subsystem = previous.get("subsystem",subsystem)
    match = re.search(r"\bsub(?:system|sytem)[ -]*(\d+)\b", lower)
    if match:
        subsystem = "Subsystem-" + match[1]
    if re.search(r"\b(all subsystems|all pages|entire icd)\b", lower):
        subsystem = ""
    published = request.get("answer_source") == "published"
    if published:
        rows = entries(request.get("active_profile", {}))
    else:
        indexed = db.execute("SELECT row_json FROM signal_entries WHERE profile_hash=? ORDER BY position",
                             (request.get("document_hash", ""),)).fetchall()
        rows = [json.loads(row[0]) for row in indexed] if indexed else entries(profile)
    def matches(row):
        if subsystem and row.get("subsystem", "") != subsystem:
            return False
        encoding = row.get("encoding", "")
        return (kind == "all" or kind == encoding or
                kind == "float" and encoding in {"float32", "float64"} or
                kind == "unresolved" and row.get("review", False) or
                kind == "unsigned" and encoding == "integer" and row.get("signed") is False or
                kind == "signed" and encoding == "integer" and row.get("signed") is True)
    rows = [row for row in rows if matches(row)]
    original_rows={row["id"]:row for row in entries(profile)}
    citations, refs, results = [], {}, []
    for row in rows:
        source = (row.get("source_hash", ""), row.get("source_page", 0))
        if source not in refs and source[0] and source[1]:
            citations.append(dict(hash=source[0], page=source[1], document=row.get("source_document") or profile.get("document", ""),
                                  excerpt=row.get("evidence", {}).get("text", row["id"])[:1000], rank=1))
            refs[source] = len(citations)
        first, last = row.get("word_first", -1), row.get("word_last", -1)
        words = row.get("printed_words") or (str(first) if first == last else f"{first}–{last}") if first >= 0 else "Review"
        pdf_definition=""
        if published:
            original=original_rows.get(row["id"])
            if not original:
                pdf_definition="No matching PDF evidence indexed"
            elif original.get("review") or any(original.get(key)!=row.get(key) for key in ("word_first","word_last","encoding","unit","scale","offset","lsw_first","channel")):
                pdf_definition=(f'PDF: {original.get("encoding") or "Unspecified"}, words {original.get("word_first")}–{original.get("word_last")}, '
                    f'{original.get("unit", "")}; scale {original.get("scale", "unspecified")}, offset {original.get("offset", "unspecified")}. '
                    f'Published: ×{row.get("scale")} + {row.get("offset")}')
        results.append(dict(id=row["id"],pdf_definition=pdf_definition, signal=row.get("source_signal_id") or row["id"],
                            subsystem=row.get("subsystem") or "Unassigned", message_id=row.get("message_id", ""),
                            words=words, encoding=row.get("encoding") or "Unspecified", unit=row.get("unit", ""),
                            status="Review" if row.get("review") else "Published" if published else "Extracted",
                            citation=refs.get(source, 0)))
    scope = subsystem or "all subsystems"
    source = "Published mappings" if published else "ICD documents"
    return dict(answer=f"{len(results)} matching {kind} fields in {scope}. Word indices are zero-based; original numbering is retained in the cited evidence.",
                mode="structured-table", citations=citations,
                table={"rows": results, "count": len(results), "scope": scope, "source": source},
                query={"kind": kind, "subsystem": subsystem}, retrieval="Structured ICD rows")
