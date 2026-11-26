# AeroTrace project context

This file belongs directly in the current AeroTrace source directory. It is the
project's durable context for future agent sessions. Update it after material
decisions, setup changes, or verified findings; retain links to detailed evidence
instead of copying logs. Never store credentials, licensed SDK contents, or
unverified claims here. Current user instructions take precedence.

Context reviewed: 5 November 2026 (Asia/Calcutta). Recheck source and current
status before treating these observations as current machine state.

## Required workflows

<!-- codebase-memory-mcp:start -->
For structural codebase exploration, use the installed `codebase-memory` skill.
<!-- codebase-memory-mcp:end -->

<!-- requested-skill-library:start -->
Use installed skills when a project requirement calls for them. For design,
frontend, animation, accessibility, UX research, video, customer discovery, or
plugin selection, read
`C:/Users/Vinay/.codex/skills/requested-skill-library/SKILL.md` and select the
relevant primary workflow and necessary supporting skills. The searchable
inventory is `C:/Users/Vinay/.codex/skill-library/inventory.json`. Read selected
skills and their relevant references before work; preserve this project's stack,
shared conventions, and accepted designs. Check actual tools and credentials for
connector-dependent entries. Installing optional agent definitions does not
authorize automatic delegation or changing the user's chosen model. Apply this
guidance to future projects as well as WeeVCrafts.
<!-- requested-skill-library:end -->

Read `C:/Users/Vinay/.codex/RTK.md` for command-output handling. Run commands
normally; use `rtk bypass` only when condensed results are unusable, as that
document specifies. Use PowerShell and literal paths for Windows file operations.

The graph project matching this checkout is `AeroTrace1553-0.5.5-Source`, not the
separate `AeroTrace1553` checkout. Start structural exploration with project/index
status, then query the relevant symbols and check cited-path coverage. On this
review, the graph generation was `2026-11-02T15:33:00Z`; documentation and scripts
were excluded, and several root files had changed metadata. Read those files
directly and verify graph freshness before relying on them. No exhaustive source
or impact audit was performed for this setup task.

## Source of truth and project shape

- Root `CMakeLists.txt` declares version **0.7.1**, despite the checkout folder's
  0.5.5 name. Read `README.md`, `docs/current-status.md`, and
  `docs/Release-0.7.1.md` first for current behavior and qualification scope.
  `docs/Release-0.6.0.md` retains the earlier software measurements.
- Preserve **C++20, Qt 6 Widgets, Qt Designer, Qwt, CMake, and Ninja**. This is a
  native Windows x64 application with an isolated Python document worker.
- `apps/desktop/`: `main.cpp` entry point, shell/controllers, models, widgets,
  tests, and Designer forms. Build/run target: `aerotrace_desktop`.
- `modules/core/`: Qt-free types, conversion, CRC-protected ATD recording/replay.
- `modules/acquisition/`: source lifecycle, DDC/simulator, capture, raw recording,
  conversion, and bounded publication. One capture worker owns SDK state.
- `modules/application/`: Qt-free profiles, analysis, replay, and exports.
- `tools/document_worker/`: PDF/table extraction, OCR, embeddings, SQLite search,
  and cited document answers in a cancellable child process.
- `scripts/`: existing build, preflight, diagnostics, packaging, and verification
  helpers. `third_party/qwt/` contains vendored Qwt source.
- `UI_Screens/`, `Concept_Boards/`, `docs/design/operator-workflow.md`, and
  `docs/design/rt-live-monitor-proposal.md` hold accepted design references.
  Edit `.ui` forms, keep controller-bound object names stable, and never edit
  generated `ui_*.h` files.

## Build and validation

Run from the repository root, using the existing helpers:

```powershell
# Build desktop and run its configured tests.
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Test

# Build/test the Qt-free backend (the helper uses out/build/core-local).
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -CoreOnly -Test

# Broader native checks, when the work requires them.
powershell -ExecutionPolicy Bypass -File scripts/preflight.ps1 -Profile Full
```

The build helper discovers an installed Qt 6 MinGW x64 kit under `C:/Qt`, uses
`C:/Qt/Tools/mingw1310_64`, CMake, and Ninja, and builds matching Qwt. It accepts
`-QtRoot`, `-CompilerRoot`, `-BuildDirectory`, and `-Jobs` overrides. Verify tool
availability before using these defaults on another machine.

Use the `desktop-local` build directory/preset; the older `desktop-release`
cache may contain another computer's paths. Rebuild Qwt and use a fresh build
directory when changing Qt/compiler kits. `CMakePresets.json` also defines
`core-release` and `backend-native-sanitized`; the sanitizer/fuzzer build is
separate from the Qt build and uses `scripts/build-verified-backend.ps1`.

CTest supplies GUI workflow/smoke/performance/document test arguments.
`QT_QPA_PLATFORM=offscreen` supports headless checks. Run checks appropriate to
the actual change; a documentation or skill install does not need an application
rebuild. See `docs/local-verification.md` and `docs/validation.md` for scope and
prerequisites. CI/CD is intentionally unconfigured in the reviewed baseline.

## Acquisition and operator behavior to preserve

- CH1/Device 0 is External; CH2/Device 1 is Internal. Logical assignments remain
  configurable. A/B are redundant bus paths within each channel.
- Connect configures devices without receiving/recording. Only explicit Start
  recording begins capture. Stop & save finalizes recording and paired CSVs,
  retaining a healthy connection. Disconnect saves an active run and releases
  devices. Launch, arrival, and settings changes never automatically record.
- Hardware is the default source; simulator use is explicit. Display filtering,
  display pause, and display queue eviction must not discard raw disk capture.
- Keep acquisition, GUI refresh, documents, and analysis ownership separate.
  Preserve bounded queues, real packet/tick data, and explicit incomplete-session
  reporting for loss, overflow, and capture/write errors.
- Raw capture, replay, and paired CSV export require no ICD. Engineering values
  require validated mappings; uncertain document extraction requires review.
- Paired exports are `_EXT.csv` / `_INT.csv`, with compact 11-column default,
  explicit legacy 42-column compatibility, and preserved raw
  recordings/integrity sidecars. Do not overwrite another recording's exports.
- The current DDC MTI adapter requests `ACE_TT_2US`: ticks represent **2,000 ns**.
  Display precision does not establish accuracy or cross-device synchronization.
- Keep licensed DDC SDK inputs external. Provider/runtime architecture, ABI,
  hash, SDK identity, and actual device evidence matter; matching export names
  alone do not prove compatibility. Consult `docs/provider-contract.md`,
  `docs/sdk-compatibility.md`, and `docs/card-time-and-diagnostics.md`.

## Qualification boundaries and pending work

The 0.6.0 software report describes a deterministic four-stream fixture with
30,026,073 messages at 50,043 messages/s. This is historical software evidence,
not a hardware run or a zero-lag guarantee. Physical simultaneous CH1 A/B + CH2
A/B, responding-RT coverage, and extended endurance remain **NOT RUN** in the
reviewed status. The earlier Windows 0x10D setup failure remains unresolved.
Use `docs/SES-Bench-Checklist.md` and fresh bench evidence to close those gates.

Replay uses the latest profile snapshot, not full historical revision playback.
Session SQLite, DuckDB/Parquet benchmarking, and isolated scientific analysis
remain separate development stages; existing document SQLite/OCR/ONNX does not
establish those features. Re-read current status before planning additional work.

## REA skill installation and usage

Requested source: https://github.com/morluto/rea.git

Pinned skill source revision: `25b62db838bca5305ef540ae33980778b0918017`.
Global skill root: `C:/Users/Vinay/.agents/skills/`.

Installed on 4 November 2026: all five skills below, including the main workflow
updated from metadata version 49 to 60. All **16 installed files** matched the
staged pinned source by SHA-256 after installation. The previous main skill and
the per-file installation manifest are saved in
`C:/Users/Vinay/.codex/skill-backups/rea-20261104-25b62db838bc-d0f8b853/`
(`reverse-engineer-anything/` and `installation.json`). No MCP configuration was
changed. Installed skills become available to the agent on the next turn.

| Skill | Intended use |
| --- | --- |
| `reverse-engineer-anything` | Shipped binaries/packages, decompilation, runtime evidence, version comparison; read its `SKILL.md` and task-specific references. |
| `rea-analysis-optimization` | Optimize REA's own providers and analysis workflows. |
| `rea-changelog-update` | Prepare REA release notes from pinned history. |
| `rea-ci-tests-audit` | Audit REA's own CI/test suites. |
| `rea-tool-design` | Design/audit REA CLI/MCP tools and Evidence contracts. |

The four maintenance workflows assume a REA source checkout and its repository
guides/scripts. Their `../../../docs/...` links refer to the **REA repository**,
not AeroTrace or user-profile documents. Resolve missing guides against the
[pinned upstream tree](https://github.com/morluto/rea/tree/25b62db838bca5305ef540ae33980778b0918017).
Do not apply REA's release process or Node stack to this project.

Use codebase-memory and source tools for ordinary AeroTrace source exploration.
Use REA when the requested investigation depends on an artifact or runtime
behavior unavailable from source. The `mcp__rea` tool catalog was available in
the installation session; use its actual advertised schemas. A skill supplies
instructions, not an analysis engine or server registration. Native providers
were not probed by this setup task. Read REA's `references/native-and-artifacts.md`
for native targets and `references/connection-and-recovery.md` only for an
observed connection/provider blocker. Do not rerun setup as a routine preflight.

Installing these skills does not authorize running hardware, attaching to an
application, changing MCP configuration, installing analysis engines, publishing,
delegating, or switching models. Preserve the user's requested task scope.

## DDC / RTPro research checkpoint — 4 November 2026

Read `docs/research/DDC-RTPro-investigation-2026-11-04.md` before changing
acquisition concurrency or SDK compatibility. The initial task requested findings
first; the user subsequently approved the full 0.6.1 implementation plan.
Detailed findings and selected identity manifests are preserved alongside it in
`docs/research/ddc-rtpro-2026-11-04/`; raw evidence and extracted vendor archives
are local-only under `out/investigations/ddc-rtpro-20261104/`.

- Supplied installer payload main executables match the installed RTPro main
  binaries by SHA-256. Legacy is x86/Qt5.13.1 with bundled SDK DLL3.6.13.1;
  NxtGen is x64/Qt6.10.2, product4.1.1/file4.1.1.142. Its registry4.1.0 and
  component4.0 fields are stale. Do not use folder/installer labels as ABI proof.
- `C:/DDC/ddccm.exe` is Card Manager4.0.3.1. The installed development SDK is
  `C:/DDC/aceXtremeSDKv3.6.15`, with headers/samples/x64 and x86 DLL3.6.15.1.
  Earlier missing-input reports for3.6.15 are historical; no new provider was
  built/qualified in this investigation. Supplied BU4.9.5 is actually4.9.5.1;
  readable header payload remains unavailable without further extraction/setup.
- DriverStore has3.6.15.1 and4.10.2.1 packages for five DDC driver families.
  No present DDC-class device or matching service was observed in the bounded
  inventory. Staged packages are not an active-driver or physical-capture result.
- Four streams are two logical-channel captures with per-message A/B provenance.
  Legacy RTPro's device-associated QThread statically reaches an MTI read with
  timeout0. This does not establish NxtGen's topology, four reader threads,
  arbitrary SDK reentrancy, or physical simultaneous-monitoring performance.
- NxtGen imports emacepl.dll but bundles none; System32 x64 DLL3.6.15.1 is a
  loader candidate, not an observed loaded module. SDK/system runtime PE sections
  match but whole-file hashes differ. Existing AeroTrace providers require exact
  runtime identity; do not weaken that contract based on version strings alone.
- AeroTrace already configures two channels/both buses, with fixed-order
  nonblocking polling on one recording worker shared with disk/decode work.
  SDK affinity/reentrancy, sustained four-stream reception, synchronized clocks,
  NxtGen protected capture internals and SDK4.9.5 ABI remain unverified.
- SDK ARINC429 DD-42992 is separate from1553 BU-69092. RTPro sample workspaces
  enable BC/RT transmission as well as MT and must not become passive-monitor
  configuration templates. No installer/vendor application/hardware was run.

## 0.6.3 connection recovery development
The approved 0.6.2 plan is being delivered as 0.6.3. Read docs/Release-0.6.3.md for current build and acceptance boundaries. Native ABI2 remains alongside ABI1. x86 helper uses a separate MSVC build; keep the desktop Qt kit unchanged. scripts/build-release-063.ps1 builds/tests/packages the review installer. Physical affected-laptop acceptance remains NOT RUN.



## 0.6.4 review build — 5 November 2026
Selectable native/helper and MT/MTI controls are in `apps/desktop/forms/ddc_method.ui`; method changes are manual. Selecting the x86 helper explicitly selects compatibility policy; pack identity checks remain in force. `scripts/build-release-064.ps1` generated `dist/installers/AeroTrace1553-0.6.4-Review-Setup-x64.exe` and its SHA-256 file. Desktop CTest passed 35/35, including the updated method-selection fixture; x86 helper and provider-pack fixture checks passed. See `docs/Release-0.6.4.md`. Physical card acceptance, interactive Qt Creator review, and runtime redistribution rights remain unverified. The review bundle is not production distribution approval.

## MIL-STD-1553B development work in progress
See `docs/mil-std-1553b-traceability.md`. Protocol structure is now separated from RT busy/fault/message-error status, while engineering values remain suppressed for those statuses. `SequenceAnalysis` supplies bounded conservative offline observations and Sessions has a versioned JSONL export. No hardware/timing qualification or standards conformance is claimed. This work is incomplete: live sequence presentation, selectable standards profiles, timing capability integration, end-to-end export verification, Qt Creator accessibility review, and a new review installer remain pending. Existing 0.6.4 installer predates these changes.

## Engineering document work in progress
`docs/engineering-document-work-in-progress.md` records partial implementation. PDF ingestion now requires explicit activation; the local 32-word example produces review-only word-range evidence. Signals support an optional expected data word count and retain compatibility with older profiles. Full reviewed-draft editing, SI conversion, reports, revision playback, 0.6.5 packaging and hardware acceptance remain outstanding. Preserve the separate uncommitted MIL-STD analysis changes.
Software verification for this partial engineering-document change: document-worker unittest suite 7/7 and desktop CTest 35/35 passed on 5 November 2026. The example-range test used the local untracked Markdown input; no new 32-word digital/scanned PDF fixtures or hardware verification were completed.

## 0.6.5 review build — 5 November 2026
`scripts/build-release-065.ps1` generated `dist/installers/AeroTrace1553-0.6.5-Review-Setup-x64.exe` and SHA-256 checksum. The example table PDF is `examples/demo-icd/Engineering-32-Word-Example.pdf` with separate simulation notes. Desktop CTest passed 35/35 and native provider/x86 helper fixture checks passed; physical qualification remains NOT RUN. 32-word simulation and four plot selectors were added; interactive Qt Creator/accessibility review and dedicated end-to-end new plot-selection verification remain NOT RUN. The example profile currently decodes all demo mappings, not only those selected for plotting. Source 0 also represents legacy records with ambiguous provenance; the simulation-only flag distinguishes modern DDC records but does not establish origin of old source-0 recordings. MSVC helper build reported warnings C4018, C4267 and C4146.


## 0.6.6 bench candidate development — 8 November 2026

See docs/Release-0.6.6.md for the bench procedure and evidence boundaries.
Preserve the demonstrated 32-bit compatibility helper, automatic MTI/MT,
and ddc-3.6.15-x86 selection. Healthy empty polls count as progress.
The helper fixture distinguishes silence from failed heartbeat, stalled
operations, missing final acknowledgment and explicit SDK cleanup failure.
Physical acceptance on the affected BU-67113U laptop remains NOT RUN.
SDK result 22 remains unresolved.

ICD Library and PDF evidence feed the same staged signal review grid.
Use explicit publication, retain the original source evidence, and
apply synthetic mappings only in verified example context. Replay loads
its own latest profile snapshot without replacing the active capture profile.
See docs/icd-documents.md for CSV and operator workflow.


Desktop CTest passed 36/36, both PDF workflow checks passed again with the
updated plot layout, and the standalone document worker passed 7/7. Use
the release note for the software throughput and latency figures. The
installer is a bench candidate until the targeted physical retest passes.

## 0.6.7 bench candidate — 8 November 2026

See docs/Release-0.6.7.md for the installer and qualification record.
The install wizard requests bench-use acceptance before destination selection.
Device Setup persists generic, example, and published-ICD simulation separately.
Use explicit publication and start; the example remains simulation-only.
Bus Analysis groups Inspector, Topology, and Alerts. Signal reports pair
engineering CSV and a PNG for one stream, with retained-live or saved-session scope.

See .codebase-memory/Release-0.6.7.md for source entry points and the
verified simulation mismatch that motivated the 0.6.7 changes.


## 0.6.8 bench candidate — 9 November 2026

See [the 0.6.8 release record](docs/Release-0.6.8.md). Offline simulation
uses RT 03 and RT 08, SA 12 receive, with 32 data words. New ATD sidecars
record fixture provenance. Keep the simulation-only profile restriction
for DDC traffic. Prior recorded example sessions retain their proven
compatibility. Use approved real ICD values to qualify DDC engineering.

Setup checks the existing AeroTrace registration and requires graceful
desktop exit before clean replacement. Preserve user data and external
DDC drivers. The compact embedded Python runtime is built from pinned
artifacts and tested with PDF extraction, OCR and local retrieval.

See .codebase-memory/Release-0.6.8.md for the 0.6.8 source entry points, verification, and pending gates.

## ICD workflow development — 10 November 2026

The build now reports the CMake project version. The document worker indexes
ordered PDF sets and stages compact ICD tables with separate route headers.
The private sample verified 54 typed fields and three evidence-only rows.
Review requires confirmation of undocumented scaling and ambiguous stream
assignments. Published-mapping chat reads the desktop profile and cites the
PDF evidence separately. Plot limit overlays use existing alert rules.

See [ICD ingestion and review](docs/icd-documents.md) for the operator flow.
The offscreen desktop suite passed 36/36. The separate private-sample
[workflow receipt](out/test-results/table-icd/table-icd-workflow.txt) verifies
MSW-first float and unsigned integer decoding, channel separation, a plot
excursion, and a page-cited published-mapping answer with synthetic words.
The native splash and the broader ICD screen redesign are awaiting design
selection. Physical engineering acceptance remains pending.

## 0.6.9 bench candidate — 10 November 2026

See [the 0.6.9 release note](docs/Release-0.6.9.md). The Windows x64 bench
installer and its SHA-256 sidecar are in `dist/installers`. The offscreen
suite passed 36/36, all provider and helper fixtures passed, and the staged
app verified startup rendering and handoff at 100% and 150% scaling. The
staged [Table-1 workflow receipt](out/test-results/069-table-icd/table-icd-workflow.txt)
uses synthetic bus words. The screen alternatives are design previews for
a later revision. Interactive upgrade and physical qualification are pending.


## 0.6.10 Subsystem Workspace — 11 November 2026

The accepted B workspace is implemented in native Qt Designer/Widgets. Mapping,
Engineering, Live Plot and docked/floating Chat share subsystem/stream scope.
Source IDs remain stable; optional subsystem/message/source-signal metadata
round-trips through profile and session JSON. Known qualified IDs can restore
missing optional metadata; other legacy mappings remain Unassigned. Replay
mapping review is read-only and retains its own profile context.

Bulk presets preview changes, preserve edits by default, and persist per named
ICD set/subsystem. Calibration reuse checks type/width/unit/route compatibility.
Changed source definitions require explicit review. Publishing a subsystem
merges other groups, validates profile/check rules and preserves the prior
profile on failure. The private Table-1 seed uses MSW first, S1 CH1, S3 CH2;
identity scaling still requires explicit confirmation.

The bundled Qwen3-1.7B Q4_K_M / llama.cpp v0.9.0 (resolved b12110) assets are
pinned in tools/document_worker/assistant-assets.lock.json. Do not commit model
weights. scripts/package-assistant.ps1 verifies archive/member/model/license
hashes. Inference is separate from the PDF worker, authenticated on loopback,
limited to two CPU threads, 20% CPU and 2.5 GiB process commit, and unloaded after
120 idle seconds. Structured chat never requires the model. Capture/low memory
keep source answers available. Conservative generated-answer checks can fall
back to evidence. No model output publishes a mapping or computes engineering.

See [0.6.10 verification](docs/Release-0.6.10.md), the
[37-check receipt](out/test-results/0610-regression/release-tests.xml),
[focused document checks](out/test-results/0610-regression/final-document-tests.xml),
[final Table-1 re-import checks](out/test-results/0610-release-table/table-icd-workflow.txt),
[packaged Table-1/RAG receipt](out/test-results/0610-package-acceptance-final/table-icd-workflow.txt),
[model budget qualification](out/test-results/0610-inference-packaged/environment.json),
and [model measurements](out/test-results/0610-inference-packaged/qualification.txt).
The CPU model benchmark used an 8 GiB process-tree commit budget on a 16 GiB
host, not a physical 8 GB target. Physical DDC qualification and actual installer
upgrade/silent/cancellation acceptance remain NOT RUN. The computer-use Setup
preview launch timed out without a targetable window; do not claim visual
installer animation acceptance. Existing installed application/data were retained.

The native splash minimum is 3,000 ms from first paint, including reduced motion;
slow initialization extends it. Inno uses SetTimer/CreateCallback with a 32-bit
Windows BOOL output and stops timers for silent/reduced-motion/shutdown cases.
Qt Network is now required for the managed local model. Current payload budget
is 2,140 files (staged 2,036). Guide generation uses the optional build-only
out/guide-venv with ReportLab 4.4.4; it is not part of the product runtime.
Use scripts/build-release-0610.ps1 for the bench release and
scripts/test-offline-assistant.ps1 for bounded packaged inference qualification.

The final bench installer is
`dist/installers/AeroTrace1553-0.6.10-Bench-Setup-x64.exe` (1,431,769,420 bytes),
with SHA-256
`4907BB35C46DB6B600844FB7EDAB747D4B94A00BCFFDFB9CFA40C57AA0EABBF8`.
Its `.sha256` and `.receipt.json` sidecars record software checks and pending
acceptance. The verified payload has 2,036 files / 1,726,761,326 bytes; 205
generated Python cache files in staging are excluded from the installer and
manifest. Final packaged splash checks measured 3,014 / 3,004 ms at 100%/150%.
The initial full compiler invocation stopped with a path error; an unchanged
retry completed successfully. No actual installation or upgrade was performed.


## 0.6.11 software acceptance — 12 November 2026

The approved stable workspace implementation is software-verified. Read
`docs/Release-0.6.11.md`. One sidebar destination now wraps Mapping, Engineering
and Analysis. Engineering uses a native raw-word preview rather than live tiles;
plots retain bounded discontinuity segments and expose larger native Qwt views.
The Assistant is an independent window with additive conversation storage and
archive/restore operations. Compact 11-column CSV is the new default; legacy
42-column export remains explicit and old export retries retain their format.

Verification: 39 unique native checks across the complete run and focused
retries, documented honestly in `out/test-results/0611-regression/aggregate.json`.
The initial workspace heartbeat check included the modal file chooser; its
timer now measures only the background conversion. Final cursor changes were
checked by the operator/workspace tests and exact packaged UI checks at both
scales. Document-worker 16/16 and fresh provider/helper fixture checks passed.
All 262,144 word/radix cases and long/quoted/oversized CSV rows passed. A
45,920,091-byte / 140,000-row CSV converted in 6,478 / 5,413 ms at 100%/150%,
with 33 / 33 ms maximum heartbeat gaps and unchanged source hashes. The plot/body
height ratio was 0.80 at 1366 x 768. Final splash durations were 3,009 / 3,002 ms.

See `out/test-results/0611-package-final-ui/`, the Table-1 receipt under
`out/test-results/0611-package/table-icd/`, and model qualification under
`out/test-results/0611-inference/`. Structured 39/13-row answers passed. The
explanatory RAG check completed with cited extractive fallback in 170,967 ms;
do not claim every generated explanation passes grounding. The constrained
model run passed with peak working set 1,996 MiB and private commit 1,041 MiB,
using the same 8 GiB process-tree budget on the 16 GiB development host.

Source entry points: `pages/engineering_setup.cpp`, `pages/analysis_tools.cpp`,
`pages/csv_tools.cpp`, `services/csv_conversion.hpp`, `pages/icd_management.cpp`
under `apps/desktop/`, and `tools/document_worker/library_history.py`.
Archiving holds publication until its asynchronous library operation completes,
protecting rollback from a second publication. Cursor pairs are scoped to plot,
signal/raw column and channel/bus. Input bases preserve raw bits; native encoding
and calibration remain separate. The graph generation predates these new files;
check coverage and read source where the graph is stale or untracked.

The frozen staged payload has 2,037 files / 1,696,035,599 bytes, no Python caches,
and retains 1,282,439,264-byte Qwen weights. The duplicate audit retains required
app-local libraries/notices. The unused 30,876,160-byte OpenCV video DLL is omitted
after exact packaged PDF/OCR checks. `out/0611-release-finalization.log` records
the successful 264.748-second installer build. The bench installer is
`dist/installers/AeroTrace1553-0.6.11-Bench-Setup-x64.exe`, 1,420,523,186 bytes,
with SHA-256 `D522AFA2A20B7D849D42C5954A6764671C0CA4E459C7AB38B66AFD3E9E7937C8`.
Its `.sha256` and `.receipt.json` sidecars retain measurements and test evidence.
Physical DDC/physical 8 GB qualification and actual installation/upgrade,
silent/cancellation and visual installer-animation acceptance remain NOT RUN.
Original 0.6.9 supplied logs/recordings and 0.6.10 installers remain unchanged.

## 0.7.0 software acceptance — 15 November 2026

Read `docs/Release-0.7.0.md`. Recorded CSV Analysis uses the native decoder,
explicit channel/bus/route and copied published rules. `services/recorded_csv.hpp`
streams full-range statistics and bounded, ordered min/max display envelopes;
time jumps and gaps remain visible. Narrowed ranges receive their own envelope
resolution. CSV row cursors identify source records. Legacy 42-column supplier
rows may contain empty trailing padding; extra nonempty cells remain errors.
Raw engineering encodings and input/display bases remain separate.

`services/session_store.cpp` retains the primary export reservation and adds
independent `.export-<format>.json` reservations for format copies. Changed
settings create pairs such as run3_hex_EXT/INT.csv; retries preserve options,
hashes and owned files. Same-directory temporary files publish without replacing
unrelated outputs. Startup discovery checks only a first valid record in the
background; export validates the prefix and labels incomplete recovery without
rewriting ATD bytes. Save feedback appears after 300 ms and blocks conflicting
save/close actions. The splash minimum is 5,000 ms from first paint.

Native CTest passed 39/39, with final frozen-build checks 4/4. Document-worker
tests passed 17/17; provider/x86-helper fixtures passed. Exact packaged startup,
slow startup, workspace and session tests passed at 100%/150% scaling. Normal
splash durations were 5,012/5,008 ms; slow startup 6,131/5,979 ms. The recorded
plot/body height ratio was 0.697974. The large CSV fixture retained every source
row and unchanged bytes, with 36/37 ms maximum measured UI heartbeat gaps.
Supplied compact captures retained 32/186 backward boundaries in selected
RT4/SA2/Bus-A streams. See `out/test-results/070-regression/`,
`out/test-results/070-package-ui-final/` and `out/070-supplied-csv-final.log`.

`tools/document_worker/retrieval_context.py` adds table/page context and bounded
follow-up retrieval without deleting library history. Complete Table-1 39/13-row
answers and confirmed routes passed. The final scoped RAG check completed in
209,627 ms with 199 streamed events and cited extractive fallback; generated
explanations are not guaranteed to pass grounding. Keep the pinned baseline
model: neither alternative in `out/070-model-eval/decision.json` qualified.
Operator wording is AeroTrace Assistant; exact model identity remains technical.
Model policy qualification passed under an 8 GiB process-tree commit budget on
the 16 GiB development host, with peak working set 1,998 MiB. Physical 8 GB,
DDC and actual installer/upgrade/silent/cancellation/animation acceptance remain
NOT RUN. The earlier incomplete packaged UI receipt is not acceptance evidence.

The frozen payload audit covers 2,037 files / 1,696,242,494 bytes, plus the
267,898-byte SHA256SUMS manifest. It contains no Python caches. Source hashes
cover 247 first-party product/build files and explicitly record the dirty tree.
Use `scripts/build-release-070.ps1`; the installer and its external receipt are
under `dist/installers`. Preserve older installers and supplier inputs.

Structural baseline artifacts are `.ua/knowledge-graph.json` and
`graphify-out/graph.json`, with limitations in their review records and
`graphify-out/GRAPH_REPORT.md`. They predate 0.7.0 product edits. The older
codebase-memory index remains insufficient for new files; check coverage and
read current source. The dashboard was started on loopback and opening its tab
was queued by Codex; this is not a claim of rendered browser inspection.

Final 0.7.0 artifact:
`dist/installers/AeroTrace1553-0.7.0-Bench-Setup-x64.exe`, 1,420,600,422 bytes,
SHA-256 `C813261760C15862F8AA3060E2A2D1D2B09364C6BBFB17854285DF5784ADB492`.
Its `.sha256` and `.receipt.json` retain artifact and software-verification
evidence. Installer compression/verification took 254.072 seconds. No actual
installation was performed. The preserved 0.6.11 installer hash was rechecked.

## Capture and ICD workspace development — 20 November 2026

The two operator destinations are Monitor & Record and ICD Library. The
selected output folder holds raw ATD, native packets where available, integrity
sidecars and the paired CSVs. Recovery is explicit and folder-scoped. Preserve
existing recordings and the ATD reader. Legacy playback UI is confined to the
existing recording fixtures.

Acquisition keeps DDC polling, the durable writer and live analysis on separate
owners. The 64 MiB writer queue stops capture on saturation and preserves a
prefix. Display and analysis skips have separate counters. Checkpoint counts
describe successful storage flushes. Use independent wire-reference evidence
to qualify capture completeness, and preserve independent channel clocks.

Indexed ICD questions remain available during recording. PDF indexing and full
generation wait for recording and saving to finish. Assistant edits persist
as drafts with operator provenance. Review and publish through the native
decoder before the next recording. Older mappings default to payload words.
Use explicit command/status selection and peer addresses for RT-to-RT layouts.

See [current status](docs/current-status.md) and the
[bench checklist](docs/SES-Bench-Checklist.md). Software reports are under
`out/test-results/capture-icd/`. The current machine's Windows inventory
returned no matching DDC device. Physical qualification remains pending.

The [final sustained fixture](out/test-results/capture-icd/acceptance-four-stream/performance.json)
passed with 3,765,504 checkpointed records, exact four-stream and paired CSV
reconciliation, 27 ms p95 / 92 ms maximum GUI heartbeat, a cited indexed answer
during capture, and PDF ingestion after capture. The final short stress run
passed in [CTest](out/test-results/capture-icd/final-latency.xml).
Keep filesystem probes out of the periodic monitor refresh. The UI checks
that the recording exists when the operator saves it.

The final portable review build is in `dist/AeroTrace1553-Capture-ICD-Review`.
The [verification receipt](out/test-results/capture-icd/final-portable-verification.json)
records the executable hash and six passing package checks at 100% and 150%.
The CTest receipts cover 39 distinct passing checks across the suite and
focused reruns. The final packaged document worker passed 18/18. This build
is staged for local bench use.

## 0.7.1 bench installer — 23 November 2026

Manual command mappings require a passing raw example per field before
publication. A separate live view plots every field for the selected command
and stream. The PDF Assistant uses text and table retrieval with local OCR.
The installer omits the generative and embedding assets.

The [release record](docs/Release-0.7.1.md) links the 40 passing regression
checks and the 75-second four-stream fixture. All 3,770,509 records reconciled
with paired CSVs while 32 chart histories were active. GUI heartbeat was
33 ms at p95 and 160 ms maximum.

The [installer receipt](dist/installers/AeroTrace1553-0.7.1-Bench-Setup-x64.exe.receipt.json)
records the 126,350,896-byte Windows x64 bench artifact. Its SHA-256 matched the
sidecar and receipt. The packaged manual workflow, PDF Assistant, and startup
passed at 100% and 150% scaling. All 18 packaged worker tests, three native
provider packs, and the x86 helper checks passed. Physical DDC and actual
install/upgrade acceptance remain pending. Staging resumed under PowerShell 7
after a nested Windows PowerShell process dropped an empty Git config value.


## 0.7.1 desktop UI Review — 23 November 2026

Monitor & Record, ICD Library, Device Setup and Preferences are direct
destinations in the native Qt shell. More tools expands inside the same
sidebar. AeroTrace Assistant runs in a persistent page that keeps document jobs
alive across navigation. Live Engineering remains a separate window by user
choice. Device Setup combines discovery and diagnostics on one scrolling page.
Treat absent devices, unverified checks and disconnected recording controls
according to actual state, even when a reference shows a different badge.

The startup overlay waits five seconds from its first paint and hands off
when the workspace is ready. Windows and app reduced-motion preferences keep
the overlay static. Keep the PDF worker isolated and preserve the receive-only
capture and save paths.

Use the release helper's OutputBaseName option for UI review artifacts so that
the prior bench artifact remains available. A local log for the previous
0.7.1 upgrade recorded 165.817 seconds from launch to Ready. Benchmark this
candidate separately on a disposable Windows installation. The current host
has no disposable Windows VM.

The first UI Review build passed [all 43 checks](out/test-results/071-20261123-135328/native.xml).
The staged executable matched that build by SHA-256. Packaged startup, mapping,
Assistant and operator checks passed at 100% and 150% scaling. Both window
sizes passed at each scale. The splash stayed visible for 5,006 ms and
5,009 ms respectively. The [five-screen comparison](out/test-results/ui-review-comparison.jpg)
uses renders from this package.

The final [75-second fixture](out/test-results/071-20261123-135328/capture/performance.json)
reconciled 3,769,419 raw records, RT records, analysis records and paired CSV
rows. All 32 field histories received data. The GUI heartbeat gap peaked at
182 ms against the 250 ms gate. The installed baseline measured 85 ms on the
same fixture. Earlier candidate runs missed the gate. Recording Setup now
suppresses its shadow during capture and save, and the channel count labels
update only when displayed text changes. Physical DDC qualification remains
pending.

The final icon pass adds labelled outline icons to operator actions and keeps
the More tools ellipsis with a separate disclosure arrow. See the
[final comparison](out/test-results/ui-review-final-comparison.jpg) for all
five states rendered by the staged app. The installed baseline and original
0.7.1 bench installer hashes were verified after staging the UI Review build.

The [final installer receipt](dist/installers/AeroTrace1553-0.7.1-UI-Review-Setup-x64.exe.receipt.json)
records 43 distinct native checks across the suite and focused reruns. The
short stress case initially missed the heartbeat gate and then passed at 94 ms.
The 75-second fixture initially reached 292 ms. Its retry passed at 244 ms
with all 3,767,839 records and paired CSV rows reconciled. Package checks
passed in both window sizes at 100% and 150% scaling. Startup measured
5,003 ms and 5,007 ms. The document worker passed all 18 packaged tests.
The UI Review installer is 126,398,351 bytes. Actual clean installation,
upgrade timing and physical DDC qualification remain pending.

## Phase Two ICD workspace — 24 November 2026

See [Phase Two](docs/Phase-Two.md) for the operator workflow and
[the synthetic Thermal Bench answer key](examples/thermal-bench/Answer-Key.md).
The sidebar opens the tools directly and can collapse to icons. Focus view
preserves the selected raw stream and exposes a large live plot view.

The guided Qt editor verifies native linear or custom conversions before
publication. Version 3 profile JSON stores formulas and the reader still
accepts earlier profiles. The same bounded expression evaluator serves
native preview and live decoding. ICD deletion uses a resumable journal and
retains recordings, chat history, external files and shared PDF copies.

The optional pinned local model remains outside the base installer. Exact
packaged Cited lookup and Local RAG were exercised with the synthetic ICD.
The grounded answer cites revision B, page 2. The document worker passed
19 tests and all 13 PDF cases, including scan OCR, revision isolation and
missing evidence. See [software verification](out/test-results/phase-two-20261124/)
and the [installer receipt](dist/installers/AeroTrace1553-0.7.1-Phase-Two-Setup-x64.exe.receipt.json).

The 75-second capture fixture compared 3,804,573 records across raw capture,
RT, engineering, indexed replay and paired CSV, with 32 live field histories
and a 230 ms maximum UI heartbeat gap against a 250 ms gate. A subsequent
editor-only compact layout and icon pass is covered by focused GUI checks.
Physical DDC and Windows clean-install or upgrade behavior remain unqualified.

## Application UI optimization — 24 November 2026

The current native shell is optimized around the two primary viewing surfaces:
Monitor & Record keeps the live table and filters as the largest continuous
workspace, while Live Plot Analysis has a visible Focus view that hides the
surrounding navigation and context panels until the operator exits focus.
Layouts remain Qt Designer/Widgets based with expanding policies and no fixed
desktop-only canvas assumptions; 100% and 150% packaged visual acceptance still
requires a fresh run after this change.

The startup overlay now measures its five-second minimum from the first painted
frame, so the startup check cannot hand off early when initialization finishes
quickly. Device Setup contains a compact Recording setup card, and the live
monitor no longer spends canvas height on a second recording setup card. CSV
Viewer is a separate sidebar destination from Exports & Reports.

Bus Analysis, the in-app ICD Assistant, and License & Security are retired from
the operator-visible shell, including their header/sidebar entry points and
shortcuts. Legacy page handlers remain source-compatible for existing fixture
routing and are not user-visible; this is not a claim that every historical
internal page implementation has been deleted. The application resource
description now uses Capture and ICD Workspace terminology.

The current codebase-memory index and graphify review artifacts must be
refreshed after material UI edits. Keep `.codebase-memory` and `graphify-out`
metadata tied to the same checkout, and treat older release sections above as
historical evidence rather than current shell behavior.
