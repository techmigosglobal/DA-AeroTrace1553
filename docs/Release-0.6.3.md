# AeroTrace 0.6.3 — connection recovery

Build-log follow-up: the supplied release run passed all 35 tests and staged its
deployment. The installer helper now discovers ISCC on PATH and in standard
user/machine locations; both release scripts accept an explicit `-Compiler`
path. Missing compiler detection occurs before the full release build. The
staging script now includes `Install-Notes.txt`, which is required by the Inno
Setup post-install information page, and the compiler wrapper quotes absolute
paths containing spaces.

The Qt `WrapVulkanHeaders` status is an optional graphics dependency probe;
AeroTrace does not require Vulkan. The Qwt MinGW warnings were corrected by
registering its value types after inline definitions. Deprecated helper path
construction and ignored diagnostic-test file-open results were corrected.
GNU Release links use one LTO partition, retaining optimization without the
Windows/Ninja parallel-make fallback. Verification: desktop rebuild and five
targeted GUI/diagnostics/protocol checks passed; the final LTO link produced no
warning and its SDK ownership test passed. The staged review installer also
compiled successfully after the packaging fix below.

This release carries the requested 0.6.2 connection work under version 0.6.3.
Physical acceptance on the affected laptop remains NOT RUN. No driver, Card
Manager assignment, physical card or RTPro session was changed during code
verification. Fixtures do not establish physical compatibility.

## Behavior

Card Manager assignments are refreshed before Connect, with per-channel mapping
selection and advanced logical-number overrides. Connection errors persist on
setup and monitor pages, with copyable details and diagnostics. Start recording
continues to require successful Connect. Partial-channel recovery is an explicit
new request. Compatibility pack selection remains distinct from qualified
automatic selection; exact runtime/provider identity is preserved.

Native providers expose ABI v2 alongside ABI v1, explicit Auto/MTI/LegacyMT modes,
typed SDK failures, incremental draining and exact resolved identity. SDK calls
and source destruction remain on a persistent connection-owned thread. Failed
cleanup blocks further connection attempts in that application instance.

A separate MSVC x86 helper uses matching 3.6.15 SDK inputs, authenticated local
control/data pipes, bounded framing/queues, sequence and CRC validation, and a
parent-owned process job. It preserves raw packet and normalized record fields.
It does not reuse RTPro's executable, dongle libraries or 3.6.13.1 runtime.

## Build installer

From the source root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-release-063.ps1
```

The script builds/tests, verifies all native provider packs and the x86 helper,
stages a fresh review deployment, then invokes the existing Inno Setup compiler.
It does not install the application or change vendor drivers. Output is
`dist/installers/AeroTrace1553-0.6.3-Review-Setup-x64.exe` and its checksum.
Review distribution is not complete production qualification or vendor
redistribution approval. The 062-labelled intermediate pack directories are
retained as build locations, not release-version assertions.

## Acceptance still required

Collect diagnostics after failed Connect on the affected laptop and compare
RTPro/AeroTrace sequentially using its actual logical device assignments. Run
known-traffic four-stream reconciliation, endurance, stop/start, unplug/reconnect,
storage pressure and each intended fallback per SES-Bench-Checklist.md.

## Code-level verification — 5 November 2026

The Windows x64 desktop compiled with Qt 6.12.0/MinGW and the x86 helper
compiled with MSVC. The full desktop CTest run passed 35/35 after the UI
regressions were corrected. Separate x86 wire/integration and x64-to-x86
integration passed, covering record provenance, replay, stop reconciliation,
child crash and bounded backpressure. Seven Python provider-pack tests and
support-bundle tests passed. A staged review installer was then compiled and
its deployment version/checksum validation passed. The final native target
rebuild had no warning or error diagnostics; Release links use one LTO
partition. Physical hardware acceptance remains NOT RUN.

