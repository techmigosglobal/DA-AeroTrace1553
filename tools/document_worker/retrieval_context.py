"""Bounded local table evidence and follow-up context for ICD retrieval."""
import re


def initialize(db):
    db.execute("""CREATE VIRTUAL TABLE IF NOT EXISTS icd_table_evidence USING fts5(
        profile_hash UNINDEXED, source_hash UNINDEXED, page UNINDEXED,
        subsystem UNINDEXED, message_id UNINDEXED, signal UNINDEXED, text)""")
    db.execute("CREATE TABLE IF NOT EXISTS icd_table_evidence_ready(hash TEXT PRIMARY KEY)")


def index(db, digest, rows):
    db.execute("DELETE FROM icd_table_evidence WHERE profile_hash=?", (digest,))
    prepared = []
    for row in rows:
        source, page = row.get("source_hash"), row.get("source_page")
        if not source or not page:
            continue
        subsystem, message = row.get("subsystem", ""), row.get("message_id", "")
        signal = row.get("source_signal_id") or row.get("id", "")
        excerpt = row.get("evidence", {}).get("text", "") or row.get("text", "")
        if not excerpt:
            continue
        heading = " / ".join(filter(None, (subsystem, message, signal)))
        prepared.append((digest, source, page, subsystem, message, signal, heading + "\n" + excerpt[:1400]))
    db.executemany("INSERT INTO icd_table_evidence VALUES(?,?,?,?,?,?,?)", prepared)
    db.execute("INSERT OR REPLACE INTO icd_table_evidence_ready VALUES(?)", (digest,))


def lookup(db, digest, rows, expression, subsystem, query):
    if not digest or not expression:
        return []
    if not db.execute("SELECT 1 FROM icd_table_evidence_ready WHERE hash=?", (digest,)).fetchone():
        with db:
            index(db, digest, rows)
    message_ids = {row.get("message_id") for row in rows if row.get("message_id")}
    message = next((candidate for candidate in sorted(message_ids)
                    if re.search(r"\bmessage\s+" + re.escape(candidate) + r"\b", query, re.I)), "")
    scope, args = "", [expression, digest]
    for field, value in (("subsystem", subsystem), ("message_id", message)):
        if value:
            scope += " AND " + field + "=?"
            args.append(value)
    return db.execute(
        "SELECT source_hash,page,text,subsystem,message_id,signal FROM icd_table_evidence "
        "WHERE icd_table_evidence MATCH ? AND profile_hash=?" + scope +
        " ORDER BY bm25(icd_table_evidence),rowid LIMIT 12", args).fetchall()


def followup(query, history):
    if not re.search(r"\b(it|its|this|that|these|those|they|their|previous|above|same)\b", query, re.I):
        return query
    for turn in reversed(history):
        if turn.get("role") != "assistant":
            continue
        rows = turn.get("result", {}).get("table", {}).get("rows", [])
        if len(rows) == 1:
            row = rows[0]
            return query + " " + " ".join(str(row.get(k, "")) for k in ("subsystem", "message_id", "signal"))
        if rows:
            break
    previous = next((turn.get("content", "") for turn in reversed(history) if turn.get("role") == "user"), "")
    return query + ("\nPrevious question: " + previous[:400] if previous else "")
