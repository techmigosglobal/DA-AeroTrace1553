# AeroTrace 1553 0.6.10 bench candidate

The Subsystem Workspace keeps the ICD, subsystem, message and stream context
visible across Mapping, Engineering, Live Plot and the dockable ICD Assistant.
Profiles and session snapshots retain optional subsystem, message ID and source
signal ID metadata. Existing IDs are preserved. Older profiles without reliable
group information appear under Unassigned; replay keeps its own profile.

## Operator workflow

1. Choose **Import ICD** and index a PDF or ordered PDF set. Select a subsystem.
2. Open **Review subsystem**. The concise grid shows signal identity, word
   location, representation, unit and review result. Expand bit fields and
   calibration when needed, or filter **Unresolved only**.
3. Use **Apply conversion preset** to preview shared word order, channels and
   confirmed scale/offset settings. It fills missing cells by default, preserves
   edits and saves reviewed choices per named ICD set/group. Calibration is reused only for compatible type, width, unit and route layouts. Re-imports retain edits;
   changed source definitions require review and explicit acceptance.
4. Preview known raw words and publish. Publishing one subsystem merges its
   reviewed rules with the active profile. Complete validation precedes
   activation; a failed publication retains the previous profile.
5. Engineering pairs native decoded values with raw words and quality. Select a
   signal to inspect its formula/evidence and choose **Plot selected signal**.
   Live Plot starts with one trace; **Compare signals** reveals the other panels.
   Existing limits, excursions, source selection and visible gaps are retained.

The private Table-1 review is seeded with the operator-confirmed MSW-first
float order, Subsystem 1 on CH1 and Subsystem 3 on CH2. Subsystem 2 keeps its
existing scope. Undocumented identity scaling still needs confirmation.
Its three untyped rows remain PDF evidence and are excluded from publication.

## Offline assistant

Choose **PDF evidence** or **Published mappings** above the conversation.
Listing/counting questions use structured ICD rows first, with complete paged
results and clickable PDF page citations. Table-1 contains 13 float fields per
subsystem and 39 across the document. Mapping answers expose published edits
beside differing PDF definitions. Chat cannot silently change a conversion.

BM25 and local MiniLM retrieve prose within the visible scope. Explanations use
a separate managed CPU process with authenticated loopback communication:

- Qwen3-1.7B Q4_K_M, maintainer GGUF revision
  `daeb8e2d528a760970442092f6bf1e55c3b659eb`, Apache-2.0.
- llama.cpp v0.9.0 resolves through its `nightly-tag.txt` to b12110, commit
  `cd959bae99ff4bed22bef2604f45241c09bbb8c8`, MIT.
- Initial 2,048-token context, two CPU threads, one request, 20% Windows Job CPU
  cap, 2.5 GiB per-process commit limit and unloading after two idle minutes.
- Lazy loading requires 3 GiB available memory; a running model unloads under
  low memory or acquisition pressure. Structured answers remain available.
- Source citations are required for generated answers. Generation failure falls
  back to retrieved evidence. Conversations retain tables, citations and stream
  events separately from conversion profiles.

Artifact/member SHA-256 hashes and license identities are pinned in
`tools/document_worker/assistant-assets.lock.json`. Packaging verifies the pins
and includes notices. Model weights are packaged, not committed to source.

## Startup and installation

The native animated splash remains visible for at least 3,000 ms measured from
its first paint. Main-window construction happens hidden. Slower initialization
extends the splash; startup errors close it immediately. Reduced motion uses the
same minimum with a static signal.

The Inno wizard includes the AeroTrace logo and a native timer-driven signal.
Silent/reduced-motion setup uses no animation, and timer cleanup runs at exit.
The actual installation progress gauge remains visible. The bundled offline
model adds approximately 1.2 GiB of model data plus its CPU runtime.

Stop and save recording, then exit AeroTrace before upgrading. Setup preserves
user data and external DDC drivers. Unattended bench setup requires
`/ACCEPTBENCHNOTICE=0.6.10`. Use `scripts/build-release-0610.ps1` to reproduce;
`scripts/prepare-assistant.ps1` bootstraps and verifies the pinned build cache before tests.

## Verification and qualification

Software verification:

- Full desktop/backend CTest: **37/37 passed**, followed by **6/6 focused PDF,
  example, OCR and ICD workflow checks** after the review changes.
- Exact bundled Python worker: **14/14 passed**, covering digital PDFs, scanned
  OCR, ordered continuation PDFs, conflicts, complete structured history,
  streamed citations and conservative fallback for unsupported generated facts.
- Private Table-1 packaged workflow: **23 checks passed**, including 39/13
  float answers, preset preview/edit preservation, re-import difference blocking,
  selected-subsystem publication, native CH1/CH2 decoding, replay separation,
  metadata round-trip, excursions and a real managed assistant request.
- The real scoped RAG request completed in **73.4 seconds** with 52 streamed
  events, then used cited evidence fallback after support validation. Small
  model output is not a substitute for source review; unsupported explanatory
  claims and new numbers are rejected conservatively.
- Packaged CPU model under an **8 GiB process-tree commit budget**: authentication,
  cancellation/restart, failed startup, low memory, recovery, capture-pressure
  unloading and 120.6-second idle unloading passed. Peak working set was
  **1,998 MiB**, with **1,041 MiB** private committed memory. Measured hash-inclusive
  startup was **23.3 s**; the short runtime probe took **15.2 s cold / 8.7 s warm**.
  These are development-host observations, not latency guarantees for an 8 GB PC.
- Packaged splash at 100%/150% scaling: **3,003 / 3,002 ms**, centered with animated
  and static frame checks. Slower initialization extended the display to
  **3,897 ms**, with successful main-window handoff.
- Three native provider packs and x86 helper protocol/integration fixtures
  passed, including the x64-to-x86 process boundary. No hardware was operated.
  Existing helper warnings C4018, C4267 and C4146 remain outside this UI scope.
- Static deployment audit: **165 native binaries**, with no unresolved direct
  non-system imports. This does not prove physical driver or hardware operation.
- Inno callback/animation script compiled successfully, including the Windows
  32-bit BOOL output parameter. A Windows UI preview launch timed out without
  exposing a targetable Setup window; visual wizard animation, actual silent
  installation, install cancellation and interactive upgrade remain **NOT RUN**.
  Read-only inspection of the existing registration passed without modifying it.

The staged deployment contains **2,036 files**, approximately **1.61 GiB** before
installer compression. Allow space for the installer, payload and temporary
files; **4 GiB free disk space is recommended** for bench setup. Actual upgrade
elapsed time remains **NOT RUN** because the installed application was retained.
The installer SHA-256 sidecar and packaging receipt are in `dist/installers`.
The installer is `AeroTrace1553-0.6.10-Bench-Setup-x64.exe`.
The compiled installer is **1,431,769,420 bytes (1.33 GiB)**. Its external
[packaging receipt](../dist/installers/AeroTrace1553-0.6.10-Bench-Setup-x64.exe.receipt.json)
records the artifact SHA-256, verified payload size and acceptance boundaries.

Physical DDC qualification remains **NOT RUN**. An 8 GB physical CPU target and
interactive upgrade must be distinguished from constrained-process software
checks on this development host. This is a bench candidate.
