# AeroTrace 0.7.1 capture and ICD workspace

**Monitor & Record** writes raw ATD, paired External/Internal CSVs, and
integrity evidence into the chosen output folder. **ICD Library** contains the
manual conversion editor and reviewed profile import. The manual editor
validates operator-supplied raw examples before publication. **Live Engineering**
opens the selected command’s values and plots during recording. See the
[0.7.1 bench release](docs/Release-0.7.1.md).
See the [current development status](docs/current-status.md) and
[operator guide](docs/Operator-Guide.pdf).

## Current UI optimization — 24 November 2026

The startup check keeps its loading overlay for five seconds from the first
paint. The operator shell prioritizes spacious Monitor & Record and Live Plot
Analysis surfaces: Device Setup contains the compact recording preparation
fields, CSV Viewer is separate from Exports & Reports, and Focus view expands
the plot canvas. Bus Analysis, the in-app ICD Assistant, and License & Security
are retired from the visible shell. The native layout remains scalable through
Qt expanding policies; fresh packaged 100%/150% visual acceptance is still a
separate review gate.

## AeroTrace 0.7.0 bench candidate

See [0.7.0 release status](docs/Release-0.7.0.md) for Recorded CSV Analysis,
format-preserving save copies, startup recovery, five-second splash and offline
Assistant evidence improvements. Qualification results and remaining gates are
recorded in the release note.

# AeroTrace 0.6.11 bench candidate

See [0.6.11 release status](docs/Release-0.6.11.md) for the stable workspace, conversion preview, independent Assistant, compact CSVs and verification scope.

# AeroTrace 0.6.10 bench candidate

See [0.6.10 release status](docs/Release-0.6.10.md) for the initial Subsystem Workspace and bundled offline model.

# AeroTrace 0.6.9 bench candidate

See [0.6.9 release status](docs/Release-0.6.9.md) for the startup splash, ICD workflow, Live Plot limits, and Windows installer.

# AeroTrace 0.6.8 bench candidate

See [0.6.8 release status](docs/Release-0.6.8.md) for the offline ICD, clean replacement setup, and bench verification.

# AeroTrace 0.6.7 bench candidate

See [0.6.7 release status](docs/Release-0.6.7.md) for installation, simulation, signal reports and qualification scope.

# AeroTrace 0.6.6 bench candidate

See [0.6.6 release status](docs/Release-0.6.6.md) and the
[signal review workflow](docs/icd-documents.md). Earlier release sections
below describe their historical scope.

# AeroTrace 0.6.3 connection recovery

See [0.6.3 delivery and installer command](docs/Release-0.6.3.md). Physical hardware acceptance remains NOT RUN. Earlier release sections below are historical.

# AeroTrace 1553 — V0.6.1 Windows x64

V0.6.1 adds dynamic SDK/provider discovery, Card Manager recovery, RT/Bus error statistics and guarded offline installation. Runtime recognition, provider support and actual channel access are separate states. See the [0.6.1 delivery record](docs/Release-0.6.1.md) and [current status](docs/current-status.md). Physical BU-67113U four-stream qualification and vendor payload redistribution approval remain release gates.

The review installer is `dist/installers/AeroTrace1553-0.6.1-Review-Setup-x64.exe` after packaging. It bundles Qt/Qwt, isolated document Python/OCR/ONNX and private provider/runtime packs. A complete production bundle additionally requires documented vendor redistribution rights and hardware qualification receipts. Review installation preserves shared DDC drivers and Card Manager. Raw monitoring, recording, replay and paired CSV exports require no ICD.

# AeroTrace 1553

[Operator guide (PDF)](docs/Operator-Guide.pdf) · [Offline HTML guide](docs/Operator-Guide.html). The offline installer includes the application runtimes. Real acquisition additionally requires the matching DDC driver and Card Manager assignments. No physical key is required.

Native hardware-oriented 1553 capture and ICD application: **C++20, Qt 6 Widgets, Qt Designer, Qwt, CMake and Ninja**. CMake compiles every Designer form with AUTOUIC; qmake and an application `.pro` file are unnecessary.

## Build and run in Qt Creator

Run `scripts/build.ps1` once to build Qwt against the installed Qt 6 kit and configure the DDC SDK. Then open the root `CMakeLists.txt` in Qt Creator, select the matching **Qt 6 MinGW 64-bit** kit, configure Release with Ninja in `out/build/desktop-local`, and select **`aerotrace_desktop`** as the Run target. The SES qualification build used Qt 6.11.1; select the Qt kit installed on the current build machine. Do not reuse `out/build/desktop-release`: its cache references another PC's Qt 6.12/Qwt paths. Rebuild Qwt and use a fresh build folder when changing Qt kits.

The Windows helpers default to the installed `C:\Qt` kit:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -PackageName AeroTrace1553-Capture-ICD -ProviderPacksRoot out/provider-packs-070 -HelperPacksRoot out/helper-packs-070 -BundleMode review
```

Launch the application or the locally built `aerotrace_desktop` target. **Connect devices** configures the selected DDC channels. In **Monitor & Record**, **Start recording** begins receiving and saving. **Stop & save** finalizes the recording and paired CSVs while retaining the connection. **Disconnect devices** saves an active recording before releasing the card. Recording starts only after an explicit operator action.

## Operator workflow

- **Device 0 / Channel 1 = External; Device 1 / Channel 2 = Internal.** A/B are redundant paths within each channel. Both channels and all four paths are enabled by default. RT statistics shows stable active RT/Bus rows, channel totals and error counters. Message routes and chronological records are alternate views. Logical IDs remain configurable.
- **Both channels**, **Device 0 · External** and **Device 1 · Internal** tabs select the table view. Bus and RT filters affect the display, not raw recording.
- Collapse the sidebar with its chevron or **Ctrl+B**. Icons retain accessible names and tooltips. The compact sidebar exposes Monitor & Record, ICD Library, Device Setup, CSV Viewer, Exports & Reports, Preferences, Help & Diagnostics, and Recover Recordings. Retired analysis, assistant, license and security destinations are not presented.
- Recording always requires **Start recording**. Changing connection settings closes the idle connection so the new settings are applied on the next explicit Connect.
- Select a row once; it remains selected as new batches arrive. **Inspect**, **Engineering** and **Plot** give direct access to the existing analysis views.
- Choose the output folder and run name before recording. **Stop & save** exports all captured rows to the paired CSVs. To finish an interrupted save, use **Recover Recordings** and select the output folder.
- Import a reviewed ICD **JSON or CSV** profile from ICD Library or press **Ctrl+I**. The active profile persists between launches. CSV Viewer is a separate destination for recorded-file inspection and plotting.
- Body text is 15 px, the monitor table is 14 px with larger rows, and the palette uses neutral surfaces, graphite, green, amber and red. Blue and purple accents are removed, including plot traces.

The self-contained package includes Qt dependencies. A bare build executable should be run through the configured Qt Creator kit or with the Qt runtime on PATH.

See [operator design and reference review](docs/design/operator-workflow.md) and [validation evidence and remaining qualification](docs/validation.md).

## ICD, live engineering and card time

See the [Phase Two workflow](docs/Phase-Two.md) for guided mapping, Focus view, and the optional Local RAG pack.

ICD Library is intentionally focused on reviewed profile import, mapping validation
and publication. The former in-app assistant, Bus Analysis, License & Security
destinations and their operator entry points are retired from the current shell.

Use [the demo ICD kit](examples/demo-icd/README.md): a digital PDF, image-only OCR PDF, matching JSON and expected conversion CSV. The digital PDF supplies nine rules for explicit review and publication. The scanned sample exercises OCR and currently sends nine uncertain rows to evidence review; it does not silently activate questionable conversions. ICD understanding creates a local searchable index and validated rules, rather than retraining model weights.

Engineering displays all active mappings with raw words, engineering values, units, `HH:MM:SS.mmm.uuu` card time and quality. Select a row and Plot selected signal, or double-click it. Monitor Columns → Individual data words exposes all 32 word columns; double-click a mapped word for engineering plotting, or plot an unmapped numeric column as raw values. Signal plots and cursor inspection stay on the selected channel/bus clock. Hours do not wrap at 24. Original ticks and packets are preserved; physical idle gaps and clock resets are reported rather than concealed.

Up to eight plotted mappings from anywhere in the 512-rule profile retain worker-owned history independently of monitor-row eviction. Raw-column plots use the retained analysis window. Cursor inspection reports an evicted raw record rather than selecting unrelated newer data.

[Timing and diagnostics](docs/card-time-and-diagnostics.md) documents the receive-only DDC API sequence, precision, rollover and clock limitations. Capture JSONL logs include SDK results, periodic queue/loss/conversion/timing metrics and final state. Application/document logs rotate locally. Run **Collect Diagnostics.cmd** to collect driver/runtime inventory and recent logs for analysis.

## DDC hardware

The Windows adapter builds against the selected installed aceXtreme SDK, loads its matching 64-bit `emacepl.dll`, and selects legacy MT or MTI receive-only mode from device capabilities. It imports no BC/RT transmit functions. SDK logical device numbers are configurable separately for channels 1 and 2; each channel has bus A/B selection. Hardware is the default source; simulator traffic requires explicit selection.

The supplied November 2 SES notes report BU-67113U, started acexusb 3.6.15.1 and logical channels 0/1. The original zero-message check was followed by loopback: 6,041 messages over 600 seconds before clock correction, then six corrected-clock runs totaling 606 messages with populated CSV verification. These are historical results from SES, not live inventory of the computer reading this source. See `.codebase-memory/RecordingFix-validation-20261102.md`.

`DDC_SDK_ROOT` is a CMake cache path. Licensed development inputs stay outside source control. Each packaged SDK/runtime pair lives in its own private directory with exact fingerprints and a compatibility manifest. The default build families are 3.6.15, 4.9.5 and 4.10.2; automatic acquisition selection requires a qualified hardware/driver/OS matrix entry. Additional packs can be installed without rebuilding the desktop. See the [provider contract](docs/provider-contract.md) and [0.6.1 delivery record](docs/Release-0.6.1.md).

## Reviewed ICD workflow

1. Open **ICD Library** and import a reviewed JSON or CSV profile.
2. Complete rules enter the review grid for explicit publication. Missing addressing, unclear indexing, unsupported types, conflicting IDs or uncertain source values remain in review.
3. Review representative raw words with the native converter before publication. Unresolved values never become live rules.
4. Approved mappings feed engineering conversion and live plots. Integer fields up to 64 bits, signed values, IEEE float32/64, packed BCD, scale/offset, word order, channel and bus constraints are supported.
5. Use the separate **CSV Viewer** to inspect recorded CSVs, retain raw source rows and open Recorded CSV Analysis without changing the active profile.

Document originals, hashes, page evidence, profiles and indexes stay in the per-user application data directory. Processing uses one cancellable child process. On Windows it has a 1 GiB commit limit, a requested 20% CPU hard cap, normal scheduling priority, two inference threads, single-threaded BLAS/OpenMP settings and a 15-minute job timeout. The document process never owns hardware state. Package setup downloads dependencies/models once; document import itself needs no network.

Supported table columns and review behavior are documented in `docs/icd-documents.md`. Arbitrary PDFs are not guaranteed to contain enough information to generate a correct engineering mapping.

## Edit every screen in Designer

All **18 screen bodies**, the main shell, page header/footer, workspace, reusable trace card and document dialog are real `.ui` files under `apps/desktop/forms/`. `pages/page_catalog.hpp` maps each screen to its form and immutable `UI_Screens/` reference.

Open a form in Qt Creator's Design mode, edit, save, build and run. Keep controller-bound object names stable; never edit generated `ui_*.h`. Promoted plot placeholders preview as widgets in Designer and become Qwt plots at runtime. The app loads `style.qss`; standalone Designer preview may not apply it.

Screen layouts follow the reference design. Runtime data, hardware controls, truthful status labels and the document dialog add necessary behavior; exact pixel identity has not been achieved or asserted.

## Structure and performance

```text
apps/desktop/
  main.cpp                    Entry point
  main_window.hpp/.cpp        Window/navigation and presentation coordination
  forms/                      All Designer layouts
  pages/                      Form bindings and page behavior
  services/profile_io.*       Strict JSON profile serialization/validation
  models/record_model.hpp     Bounded raw table model
  widgets/plot.hpp             Qwt plotting and native fallback
  tests/gui_checks.cpp        Navigation, integration and load checks
modules/
  core/                       Qt-free types, conversion, CRC recording/replay
  acquisition/                DDC or simulator polling, durable writing and live analysis
  application/                Qt-free profile, replay, analysis and export services
tools/document_worker/        Isolated PDF/OCR/embedding/retrieval process
scripts/                      Build, setup, package and comparison tools
```

One worker owns the SDK lifecycle. A separate writer consumes a byte-bounded
queue and checkpoints ATD and native packets about once per second and at
close. Live analysis and the 8,192-record display queue have independent
limits. Their skips are counted separately. Writer queue saturation, disk
failure, SDK loss and overflow stop capture and identify the output as incomplete.

Profiles contain at most 512 signals. Address routing avoids decoding every mapping against every record. History is bounded to 8192 valid samples per signal; up to eight traces publish at most 1024 min/max envelope points each. The raw table holds 5000 records; the analysis cache trims at 200000 records. Full raw data stays on disk. Large-session streaming/indexing and power-loss qualification remain release work.

Release builds use CMake's optimized flags and supported interprocedural optimization. The latest validation report records measured software throughput and GUI latency, separately from unperformed hardware qualification.

## Recordings, exports and checks

ATD2 preserves source, raw device ticks, tick units, host monotonic time, vendor block status, second command/status words and validity. The ATD reader also supports ATD1. DDC recordings retain available original per-channel packets, a profile snapshot and final integrity status. Hardware clock alignment across devices still requires bench calibration.

Exports include source provenance, time bounds and SHA-256 sidecars. Engineering CSV uses the active multi-signal profile. For a full raw export, use **Stop & save** or the folder-scoped recovery tool.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -CoreOnly -Test
```

The backend builds independently without Qt/Qwt. GUI test modes are `--workflow-test`, `--smoke-test`, `--performance-test` and `--document-workflow-test`; CTest supplies their arguments. Headless checks use `QT_QPA_PLATFORM=offscreen`.

See `docs/validation.md`, `docs/implementation-status.md`, `docs/architecture.md` and `docs/compatibility-matrix.md`. RHEL/DDC integration, physical cryptographic licensing, full protocol reconstruction, representative-ICD acceptance, long-duration hardware load tests, installers/signing and production release gates remain explicitly unqualified.

## Recording/CSV correction

Paired `_EXT.csv` / `_INT.csv` exports default to the compact 11-column format.
Choose the legacy 42-column format for consumers using the supplied DDC layout.
Each channel keeps its own time deltas. Export names are reserved to protect
existing files, and the raw ATD, packet files and integrity sidecars are retained.

DDC MTI packet timestamps follow the selected native TTRES setting. The adapter requests `ACE_TT_2US` and preserves ticks with a 2000 ns resolution; it does not assume all DDC Chapter 10 packets use 100 ns. SDK Windows caller-buffer release returning `ACE_ERR_NOT_SUPPORTED` is a no-op only while the returned packet pointer remains the caller-owned buffer. Other release failures and SDK-owned packet failures remain capture errors.

## 0.5.5 installation

The installer requires administrator rights and installs in `C:\Program Files\AeroTrace1553`. Recordings, preferences, profiles and logs remain outside the application installation. Uninstall removes the installed runtime trees and their generated Python caches. Build and package tools are excluded from the source ZIP; run `scripts/build.ps1` to regenerate the local build.

### Local qualification and SDK providers

This development source includes [versioned SDK provider DLLs](docs/provider-contract.md) and [native Windows preflight, Qt model tests, sanitizers, fuzzing and a passive bench kit](docs/local-verification.md). No CI/CD workflows are configured. Run `scripts/preflight.ps1 -Profile Full` for local evidence; unavailable SDK packages are BLOCKED and physical qualification remains NOT RUN until bench acceptance.
