# ICD Assistant: learning, conversations and conversion

Open **ICD chat** in the application header or press **Ctrl+J**. It is a separate modeless window, so capture and monitoring continue while it is open.

## What learning means

Importing a PDF stores a content-hashed local copy, page text/OCR evidence, searchable BM25/MiniLM indexes and extracted conversion definitions. Complete supported definitions are validated before automatic activation. Uncertain indexing, bit fields, numeric types, direction, word order or scaling remain in review. This is document indexing and rule configuration; no model weights are retrained.

The Chat tab answers about the selected ICD. Questions about known units/scales/mappings receive compact answers from validated definitions. Other questions retrieve supporting passages from that PDF. Replies include source names/pages. A configured loopback language-model server can synthesize prose; none is required for evidence search or conversion, and no generative weights are bundled.

Follow-ups such as “What is its scale?” use that ICD's own conversation context. Changing the selector restores a different conversation and restricts lexical/semantic retrieval to that document's hash. An identically named signal in another ICD cannot supply its unit or formula. Conversation records remain in local SQLite; the latest two exchanges are displayed and model context is bounded separately.

## Using the rules

1. Browse/drop an ICD PDF and index it, or choose an already indexed document.
2. Chat to inspect signal definitions, addresses, units, encoding and scaling. Open PDF jumps to source evidence.
3. In **Mappings and evidence**, inspect uncertain candidates before checking and activating them. Complete new digital imports retain automatic activation. Merely switching chat documents or asking questions does not change the active conversion profile; the scope label distinguishes a chat-only document from the active conversion ICD.
4. During capture, the validated profile is applied on the acquisition worker at a recorded revision boundary. Raw recording continues independently of OCR and chat. Engineering and plots use the approved mathematical definitions, not language-model prose.
5. After capture/simulation, load the recording and apply the required ICD profile to decode and plot the recorded words. Reprocessing does not require retransmitting the bus traffic.

Free-form instructions in chat do not silently rewrite conversion rules. **Preview action** accepts explicit clarifications such as `Set TEMP scale to 0.01` or `Plot TEMP`. **Apply reviewed action** validates the current profile and selected ICD before applying the change. Conversion edits require capture to be stopped. See [task automation](task-automation.md) for commands, operator provenance, workspaces and batch reports. An arbitrary PDF is not guaranteed to yield a complete automatic profile: missing or ambiguous engineering definitions must not be guessed.

## Storage and execution

`%LOCALAPPDATA%/AeroTrace/AeroTrace1553/Documents` contains cached PDFs, indexed evidence, per-document profiles and `documents.sqlite` (including per-ICD conversations). `active-profile.json` separately persists the active conversion profile. The original PDF filename and SHA-256 identify the revision. Application/document job diagnostics are in the existing local Logs directory.

One bounded child process handles each document/library/question job. Its existing memory/CPU/time limits remain in place. It cannot own hardware SDK handles. The capture worker, bounded queues, active plot subscriptions and original card timestamps remain unchanged.

## Verification

Worker tests use two PDFs with conflicting TEMP units to verify strict document scope, cited retrieval, follow-up resolution, history restoration and refusal of an unknown document hash. GUI integration checks a separate modeless assistant, persisted conversation after reopening, unchanged conversion rules after chatting, continued capture during document work, ninth-field live plotting and conversion/plotting of a completed recording.
