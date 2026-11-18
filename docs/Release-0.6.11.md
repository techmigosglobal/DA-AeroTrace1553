# AeroTrace 1553 0.6.11 bench candidate

This revision builds on the native Subsystem Workspace in 0.6.10. It consolidates
Mapping, Engineering and Analysis under one **ICD & Analysis** navigation item.
Device connection, SDK configuration and raw recording retain their existing paths.

## Operator changes

- Engineering presents stored mappings and a native conversion preview instead
  of changing live numeric tiles. Select a field, enter its raw words in the
  selected base, or use a selected saved record. Encoding and scale determine
  conversion; hexadecimal/decimal/binary/octal determine representation only.
- Analysis starts with one large trace. **Tools** provides cursor, pan, zoom,
  zoom back, Follow, axes/range, limits, statistics, Focus plot and data export.
  Compare enables up to four separate trace panes on the selected stream.
- Plot status text has bounded space. Large values use compact notation. Live
  updates preserve navigation, scroll and selection. The native trace history
  remains bounded to 8,192 entries per selected signal/stream, retaining gaps
  across backward timestamps. Statistics use retained source samples, not the
  min/max display envelope. They do not describe an entire long recording.
  Vertical clock annotations are bounded to 32 per trace; every discontinuity
  still separates the trace into segments. A/B cursors reset when changing the
  trace or channel/bus, so different units or device clocks are not subtracted.
- **Manage ICDs** supports rename, reviewed replacement, archive and restore.
  Archive previews active mapping removal and retains historical conversations,
  source evidence and recording profiles. Original supplier PDFs are untouched.
- The Assistant opens separately with saved conversations, search, New chat,
  older-message loading, citations and a multiline composer. Ctrl+Enter sends.
  New chats inherit workspace scope; existing threads retain their own scope.
  Engineering references and native previews are labeled separately from PDFs.
- Structured table answers remain complete. Follow-up explanation context is
  bounded with the pinned runtime tokenizer. Unsupported generated statements
  still fall back to cited evidence; model output never activates mappings.

## CSV workflow

Stop & save defaults to the supplied compact layout:

`Time,DeltaT,BlockStatus,MessageType,Bus,Rx_Cmd,Tx_Cmd,Tx_Status,Data,Rx_Status,BSW_err_str`

Command cells include readable RT/SA/WC descriptions. Encoded WC 00 remains
distinct from its decoded 32-word meaning. Payload words share one Data cell;
absent statuses remain blank. The legacy 42-column layout is selectable.

Time uses zero-based elapsed days, hours, minutes, seconds and microseconds from
the stored relative card clock. No day or wall-clock offset is invented.
Signed microsecond deltas preserve backward movement. Paired EXT/INT files,
full-recording scope, empty-channel headers, ownership checks and ATD retention
remain in place. Existing export retries reuse their saved layout/base; older
manifests without layout metadata retain legacy behavior.

**Sessions / Exports → Open / convert saved CSV** provides 100-row previews and
cancellable background conversion of compact and legacy files. Export a new raw
copy in another base, or explicitly select published rules and channel to write
an engineering CSV. Originals are never overwritten. Imported CSV provenance is
not equivalent to an integrity-checked ATD recording.

## Compatibility and offline payload

Profiles and session snapshots add optional ICD set/revision ownership fields.
Legacy mappings with uncertain ownership remain unassigned rather than being
removed by a guess. Conversation migration is additive and retains prior turns,
structured tables and citations. Archive is reversible; evidence is not purged.

The single offline bundle retains Qwen3-1.7B Q4_K_M and llama.cpp v0.9.0 / b12110,
with the existing revision/hash pins, license notices, authenticated loopback,
2,048-token context, two threads, 20% CPU cap, 2.5 GiB process commit limit and
two-minute idle unload. Capture/low-memory fallback remains available. The model
weights alone are 1,282,439,264 bytes; this revision does not claim a major size
reduction. Python caches and build/test material are excluded from the installer.

The animated startup splash retains its three-second minimum from first paint
and its reduced-motion state. Installer animation and bench-use requirements
retain the existing implementation.

## Verification record

All 39 unique native desktop regression checks passed. The complete run passed
38 checks; the workspace check passed on a focused retry after its test timer
was corrected to exclude the modal save-file chooser. Both original results
are retained in `out/test-results/0611-regression/aggregate.json`. Final targeted
checks also cover cursor-pair resets when the selected trace or channel changes.
Document-worker checks passed
16/16, including digital/scanned PDF extraction, ordered sets, structured table
answers, retained conversation migration, archive/restore and source labeling.
Fresh native provider packs and x86 helper protocol/integration checks passed;
these use software fixtures, not a connected card.

Compact CSV checks cover a literal expected row, missing statuses, 32-word
messages, signed backward deltas, quoting and all 262,144 word/radix round trips.
The GUI workflow converts a 45,920,091-byte / 140,000-row CSV in the background,
cancels without publishing a partial file and verifies the source hash is unchanged.
The focused run measured 13,047 ms for conversion and a maximum 47 ms event-loop
gap during that job. The file chooser was measured separately at 1,806 ms.
These are development-host observations, not an acquisition latency guarantee.
The large plot measured 0.80 of the analysis body height at 1366 × 768. Changing
float32 extremes and long status strings retained stable geometry.

The exact packaged workspace passed at 100% and 150% scaling, including cursor
scope, preserved geometry, large CSV conversion, cancellation and source hashes.
The final packaged conversions took 6,478 / 5,413 ms, with maximum event-loop
gaps of 33 / 33 ms. The splash remained visible for 3,009 / 3,002 ms; animated,
static reduced-motion and slower initialization checks passed.

Table-1 retained its 54 typed mappings, all 39 float fields and 13-field subsystem
answers, confirmed channel assignments, manual edits, citations and independent
replay profile. The supplier PDF hash remained unchanged. The scoped explanatory
RAG exercise streamed 52 events and completed in 170,967 ms with the guarded
extractive fallback. Structured table answers did not require generation.

Packaged model qualification passed under an 8 GiB process-tree commit budget
on the 16 GiB development host. Cold startup, including hash verification, took
39,903 ms; the bounded 34-token cold/warm responses took 17,572 / 8,672 ms.
Peak working set was 1,996 MiB and private commit was 1,041 MiB. Authentication,
single-request ownership, cancellation, memory/acquisition pressure and recovery
passed. Idle unload was observed at 120,439 ms. These measurements do not emulate
operating-system pressure or establish performance on a physical 8 GB laptop.

See `out/test-results/0611-regression/aggregate.json`,
`out/test-results/0611-package-final-ui/`, and `out/test-results/0611-inference/`.
The installer, its `.sha256` and `.receipt.json` sidecars are published locally as
`dist/installers/AeroTrace1553-0.6.11-Bench-Setup-x64.exe`. The external receipt
records final sizes, payload audit, migration checks and installer hash; it avoids
embedding the installer's own hash into its input payload.

The supplied 0.6.9 recordings are review evidence, not qualification of this build.
Their exported streams contain backward deltas (including -131,014 µs). This
release exposes discontinuities; it does not repair the original capture clock
or establish synchronized device clocks. Physical DDC qualification, physical
8 GB target performance and actual installer upgrade acceptance require fresh,
separately recorded results.

## Artifact measurements

The bench installer is 1,420,523,186 bytes. Its SHA-256 is
`D522AFA2A20B7D849D42C5954A6764671C0CA4E459C7AB38B66AFD3E9E7937C8`.
The verified manifest covers 2,037 files / 1,696,035,599 bytes. Including that
manifest, the uncompressed payload has 2,038 files / 1,696,303,488 bytes. This
does not include generated uninstall data, filesystem allocation or temporary
upgrade workspace. Actual installation and upgrade time were not measured.

The unused OpenCV video backend removal saves 30,876,160 bytes. The duplicate
audit retains 1,186,146 bytes of repeated app-local libraries and notices because
their separate loader locations and license attribution remain necessary. No
Python caches are in the final payload. Model weights remain 1,282,439,264 bytes.
Installer compression and verification took 264.748 seconds. Artifact size/hash
measurements are appended to this source release record after packaging; the
installed release note points to the external receipt to avoid a self-hash cycle.
