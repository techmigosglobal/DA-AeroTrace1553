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

- Root `CMakeLists.txt` declares version **0.6.1**, despite the checkout folder's
  0.5.5 name. Read `README.md`, `docs/current-status.md`, and
  `docs/Release-0.6.1.md` first for current behavior and qualification scope.
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
- Paired exports are `_EXT.csv` / `_INT.csv`, with 42 fields and preserved raw
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
