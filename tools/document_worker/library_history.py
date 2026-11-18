"""Additive ICD library and conversation persistence. No supplier files are deleted."""
import json
import re
import uuid
from datetime import datetime, timezone


def now():
    return datetime.now(timezone.utc).isoformat()


def migrate(db):
    db.execute("CREATE TABLE IF NOT EXISTS library_state(hash TEXT PRIMARY KEY, set_id TEXT NOT NULL, archived INTEGER NOT NULL DEFAULT 0, display_name TEXT)")
    db.execute("CREATE TABLE IF NOT EXISTS conversations(id TEXT PRIMARY KEY, hash TEXT NOT NULL, title TEXT NOT NULL, subsystem TEXT NOT NULL DEFAULT '', answer_source TEXT NOT NULL DEFAULT 'documents', updated_utc TEXT NOT NULL)")
    columns = {row[1] for row in db.execute("PRAGMA table_info(chat_messages)")}
    if "conversation_id" not in columns:
        db.execute("ALTER TABLE chat_messages ADD COLUMN conversation_id TEXT")
    db.execute("CREATE INDEX IF NOT EXISTS conversation_messages ON chat_messages(conversation_id,id)")
    for digest, in db.execute("SELECT DISTINCT hash FROM chat_messages WHERE conversation_id IS NULL").fetchall():
        cid = "legacy-" + digest
        db.execute("INSERT OR IGNORE INTO conversations VALUES(?,?,?,'','documents',?)", (cid,digest,"Previous ICD conversation",now()))
        db.execute("UPDATE chat_messages SET conversation_id=? WHERE hash=? AND conversation_id IS NULL", (cid,digest))
    db.commit()


def state(db, digest):
    row = db.execute("SELECT set_id,archived,display_name FROM library_state WHERE hash=?", (digest,)).fetchone()
    return dict(set_id=row[0], archived=bool(row[1]), display_name=row[2]) if row else dict(set_id="icd-"+digest,archived=False,display_name=None)


def conversations(db, digest=None):
    query = "SELECT c.id,c.hash,c.title,c.subsystem,c.answer_source,c.updated_utc,COALESCE(l.archived,0) FROM conversations c LEFT JOIN library_state l ON l.hash=c.hash"
    args = ()
    if digest is not None:
        query += " WHERE c.hash=? AND (EXISTS(SELECT 1 FROM chat_messages m WHERE m.conversation_id=c.id) OR c.title='New conversation')"
        args = (digest,)
    if digest is None:
        query += " WHERE EXISTS(SELECT 1 FROM chat_messages m WHERE m.conversation_id=c.id) OR c.title='New conversation'"
    return [dict(id=i,document_hash=h,title=t,subsystem=s,answer_source=a,updated_utc=u,archived=bool(arch))
            for i,h,t,s,a,u,arch in db.execute(query+" ORDER BY c.updated_utc DESC LIMIT 500",args)]


def resolve(db, digest, cid=None):
    if cid:
        row = db.execute("SELECT hash FROM conversations WHERE id=?", (cid,)).fetchone()
        if not row or row[0] != digest:
            raise ValueError("Conversation does not belong to the selected ICD")
        return cid
    cid = "legacy-"+digest
    db.execute("INSERT OR IGNORE INTO conversations VALUES(?,?,?,'','documents',?)", (cid,digest,"ICD conversation",now()))
    db.commit()
    return cid


def history(db, digest, cid=None, before=0, limit=60):
    cid = resolve(db,digest,cid)
    clause = " AND m.id<?" if before else ""
    args = (cid,int(before),min(100,int(limit))) if before else (cid,min(100,int(limit)))
    rows = db.execute("SELECT m.id,m.role,m.content,m.utc,r.result_json FROM chat_messages m LEFT JOIN chat_results r ON r.message_id=m.id WHERE m.conversation_id=?"+clause+" ORDER BY m.id DESC LIMIT ?",args).fetchall()
    selected=[];size=0
    for row in rows:
        cost=len((row[2]+(row[4] or "")).encode("utf-8"))
        if len(selected)>=2 and size+cost>1024*1024:
            break
        selected.append(row);size+=cost
    return [dict(message_id=i,role=role,content=text,utc=utc,result=json.loads(result) if result else {}) for i,role,text,utc,result in reversed(selected)]


def operate(db, request):
    op = request["op"]
    digest = str(request.get("document_hash", ""))
    if op == "conversations":
        return dict(conversations=conversations(db))
    if op == "conversation_load":
        cid = request.get("conversation_id", "")
        row = db.execute("SELECT hash,subsystem,answer_source FROM conversations WHERE id=?", (cid,)).fetchone()
        if not row:
            raise ValueError("Conversation no longer exists")
        digest,subsystem,source = row
        return dict(document_hash=digest,conversation_id=cid,subsystem=subsystem,answer_source=source,
                    history=history(db,digest,cid,request.get("before",0)),append_history=bool(request.get("before")),conversations=conversations(db),archived=state(db,digest)["archived"])
    if op == "conversation_new":
        if digest and not db.execute("SELECT 1 FROM documents WHERE hash=?",(digest,)).fetchone():
            raise ValueError("Select an indexed ICD")
        cid = uuid.uuid4().hex
        db.execute("INSERT INTO conversations VALUES(?,?,?,?,?,?)",(cid,digest,"New conversation",str(request.get("subsystem",""))[:200],request.get("answer_source","documents"),now()))
        db.commit()
        return dict(document_hash=digest,conversation_id=cid,history=[],subsystem=str(request.get("subsystem","")),answer_source=request.get("answer_source","documents"),conversations=conversations(db))
    if not re.fullmatch(r"[0-9a-f]{64}",digest) or not db.execute("SELECT 1 FROM documents WHERE hash=?",(digest,)).fetchone():
        raise ValueError("Select an indexed ICD")
    current = state(db,digest)
    db.execute("INSERT OR IGNORE INTO library_state(hash,set_id) VALUES(?,?)", (digest,current["set_id"]))
    if op == "library_rename":
        name = str(request.get("name", "")).strip()
        if not name or len(name)>200:
            raise ValueError("Enter an ICD name of 1–200 characters")
        db.execute("UPDATE library_state SET display_name=? WHERE hash=?", (name,digest))
    elif op in {"library_archive","library_restore"}:
        db.execute("UPDATE library_state SET archived=? WHERE hash=?", (int(op=="library_archive"),digest))
    elif op == "library_revision":
        previous = request.get("previous_hash", "")
        if not db.execute("SELECT 1 FROM documents WHERE hash=?",(previous,)).fetchone():
            raise ValueError("The previous ICD revision is unavailable")
        db.execute("UPDATE library_state SET set_id=? WHERE hash=?",(state(db,previous)["set_id"],digest))
        # Keep the prior revision visible until its replacement has been reviewed.
    else:
        raise ValueError("Unsupported library operation")
    db.commit()
    return dict(document_hash=digest,library_changed=True)
