# AeroTrace 0.7.0 bench candidate

This release builds on 0.6.11 using native Qt Widgets, Qwt and the existing
conversion service. Device connection, SDK selection and raw ATD capture retain
their existing behavior. Physical DDC qualification remains separate.

## Operator changes

- The startup splash lasts at least five seconds from its first visible paint.
  Reduced motion uses a static splash. Fatal initialization errors surface
  immediately. Recovery discovery runs after startup without connecting devices
  or loading the Assistant model.
- Operator wording uses AeroTrace Assistant. Technical asset manifests and
  third-party notices retain the model's exact identity.
- Engineering and plot values use at most six decimal places, with scientific
  notation for very large or small values. Native calculations, raw words,
  timestamps and machine-readable exports retain full precision.
- Open CSV Viewer is available from Sessions and Exports. A saved session offers
  its verified External/Internal files directly. Preview pages retain original
  cells in tooltips and full row details.
  Historical legacy rows with empty trailing padding are accepted. New converted
  copies use the 42 named fields; extra nonempty fields are rejected.
- Plot selected opens Recorded CSV Analysis without starting capture. Select a
  channel and one message route. Raw words require no ICD; engineering traces
  use an explicitly selected published mapping snapshot and native decoding.
  CSV provenance is labelled separately from validated ATD replay.
- Streaming analysis computes statistics over every matching valid row in the
  scanned range. A bounded first/min/max/last envelope limits display memory.
  Invalid values and backward card times break traces. Cursor labels identify
  original CSV rows. Highly fragmented data may exceed the display budget;
  narrow the scan range when the omission notice appears. Statistics still
  cover the entire scan. Envelope CSV exports are labelled as plotted subsets.
- Saves lasting more than 300 ms show stopping, writing or verification status.
  Conflicting save/close actions remain blocked while work runs in the background.
- Selecting a new radix or layout creates another paired export from retained
  ATD data. For example, run3's octal files remain intact while hexadecimal
  copies use run3_hex_EXT.csv and run3_hex_INT.csv. Layout/collision suffixes
  prevent overwrites. Retrying a reservation retains its original options.
- Retained nonempty recordings without a completed export appear after startup.
  Save CSVs, Review in Sessions and Later preserve the source recording.
  An interrupted capture exports only its CRC-valid prefix and is labelled
  incomplete. Original raw bytes are never repaired or replaced.

## Offline evidence and model decision

The worker adds a local table-evidence index containing subsystem, message,
signal and source-page context. Scoped retrieval ranks table rows alongside
page passages. Follow-up references use bounded conversation context. Existing
structured table answers remain ahead of model generation; indexes are additive
and conversations, reviewed mappings and original PDFs are retained.

The benchmark compared pinned Q4_K_M Qwen3-1.7B, Qwen2.5-1.5B-Instruct and
SmolLM3-3B on the same 40 synthetic ICD questions and evidence. Each process
used two CPU threads, a 20% CPU cap, 2,048-token context and 2.5 GiB commit cap.
The strict one-fact/one-citation score was 12/40, 5/40 and 0/40 respectively.
Warm p95 times were 17.073, 9.575 and 28.390 seconds; peak process commit was
1,167,187,968, 900,374,528 and 1,783,910,400 bytes.

This is a deliberately constrained 20-output-token synthesis test. It penalizes
paraphrases and truncated citations and is not a general intelligence score or
a PDF ingestion benchmark. Review of the stored answers also finds unsupported
scope/encoding claims in both alternatives. SmolLM3 exceeds the permitted p95
increase independently of scoring. Neither qualifies for replacement, so the
offline installer retains the pinned baseline with improved retrieval. These
measurements used the development host, not a physical 8 GB target. The stored
reports under out/070-model-eval retain every prompt, answer, hash and timing.

Packaged inference policy qualification passed under an 8 GiB process-tree
commit budget on the 16 GiB development host. Cold startup including hash checks
took 36,044 ms; bounded cold/warm responses took 19,023/12,590 ms. Peak working
set was 1,998 MiB and private commit 1,042 MiB. Cancellation, failed startup,
authentication, memory/capture-pressure fallback and idle unloading passed;
idle unload was observed after 120,938 ms. These are software policy checks,
not physical memory or device-throughput qualification.

## Verification and delivery

The full native suite passed 39/39; the final frozen-build export, recovery,
CSV and workspace checks passed 4/4. The document worker passed 17/17, including
digital/scanned PDFs, additive conversation migration, archive/restore and
scoped table retrieval. Fresh native provider packs and x86 helper/cross-ABI
fixtures passed. This is software verification, not physical card acceptance.

The first workspace check exposed insufficient Recorded CSV canvas space.
Moving scan controls behind Tools fixed it; the final chart/body ratio is
0.697974 at 1366 x 768, with optional panels closed. Live Analysis remains 0.80.
Eight exact packaged UI checks passed at 100%/150% scaling: startup, workspace,
save/recovery and slower startup. Animated/static frames and the five-second
minimum passed. A prior interrupted UI run has an incomplete receipt and is
excluded from acceptance; the final receipts are under
out/test-results/070-package-ui-final.

The 45,920,091-byte / 140,000-row fixture converted in 6,786/8,933 ms at
100%/150%, with maximum measured GUI heartbeat gaps of 36/37 ms. Recorded CSV
statistics include every matching row; narrow-range resolution, cancellation,
cursor row identity, full precision and source preservation passed. All
262,144 word/radix cases and missing status, RT-to-RT, malformed/quoted/oversized
row checks passed. Supplied 42-column rows with seven empty padding fields now
load; nonempty trailing fields remain an error. Supplied compact captures
retained 32/186 backward-time boundaries in their selected RT4/SA2/Bus-A routes,
with all three tested source-file SHA-256 hashes unchanged.

The final Table-1 workflow passed complete 39/13-field answers, CH1/CH2 routing,
native float/integer previews, bulk preset/edit preservation, publication merge,
replay snapshots and page-cited published mappings. The scoped explanatory
question started the managed model and completed in 209,627 ms with 199 streamed
events, then retained cited extractive evidence after grounding checks. An
earlier concurrent run did not start generation and is retained separately.
Explanatory generation remains slow and is not guaranteed to pass grounding;
structured answers do not require it.

The offline bench installer is named AeroTrace1553-0.7.0-Bench-Setup-x64.exe.
Its adjacent .sha256 and .receipt.json record final artifact measurements and
verification evidence. The source receipt hashes 247 first-party product/build
files and records the uncommitted working-tree state. Previous installers,
supplier PDFs and original captures are retained.

Physical DDC and physical 8 GB qualification remain NOT RUN. Actual installation,
upgrade, silent setup, cancellation and visual installer-animation acceptance
also remain NOT RUN. Existing Inno animation/reduced-motion code and real
installation progress are retained; compilation alone does not verify the
interactive wizard. No actual installation or upgrade is claimed.

The structural baseline is documented in graphify-out/GRAPH_REPORT.md and
.ua/review.json. Both graphs exclude supplier documents, recordings, licensed
payloads, model weights and build output; they predate the 0.7.0 edits.

## Final artifact measurements

The bench installer is 1,420,600,422 bytes, SHA-256
`C813261760C15862F8AA3060E2A2D1D2B09364C6BBFB17854285DF5784ADB492`.
Compression and installer verification took 254.072 seconds.

The verified payload contains 2,037 files / 1,696,242,494 bytes. Including the
267,898-byte checksum manifest, the uncompressed payload contains 2,038 files /
1,696,510,392 bytes. Manifest SHA-256 is
`EC9D9132D70C717AFB86754CB2B9A0EC07F5EB16EBADD18DA919C6EA73BA1D60`.
Weights remain 1,282,439,264 bytes. The duplicate audit retains 1,186,146 bytes
of necessary app-local libraries/notices; no Python caches are present.

Disk figures exclude filesystem allocation, uninstall data and temporary upgrade
workspace. Actual installation/upgrade time was not measured. The previous 0.6.11
installer still hashes to
`D522AFA2A20B7D849D42C5954A6764671C0CA4E459C7AB38B66AFD3E9E7937C8`.
These artifact measurements were appended to the source release record after
packaging; the installed copy points to the external receipt to avoid self-hash
cycles.
