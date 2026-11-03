# AeroTrace 1553 — 0.5.5 Windows hardware build

The supplied SES evidence records Qt 6.11.1, aceXtreme SDK 3.6.15 and BU-67113U A-path loopback. A ten-minute receive-stability run and repeated corrected-clock captures succeeded; responding RTs, Bus B, four-stream simultaneous traffic, endurance and reconnect remain qualification gates. See [current status](docs/current-status.md) and [the bench matrix](docs/SES-Bench-Checklist.md).

- Installer for this PC: `dist/installers/AeroTrace1553-SES-Qt611-DDC315-Setup-x64.exe` after packaging.
- Operation: [Operator guide](docs/Operator-Guide.md), including section 16.
- Bench procedure: [SES checklist](docs/SES-Bench-Checklist.md).
- Raw monitoring, recording, replay and paired CSV exports require no ICD.

# AeroTrace 1553

[Operator guide (PDF)](docs/Operator-Guide.pdf) · [Offline HTML guide](docs/Operator-Guide.html). The offline installer includes the application runtimes. Real acquisition additionally requires the matching DDC driver and Card Manager assignments. No physical key is required.

Native hardware-oriented bus analysis application: **C++20, Qt 6 Widgets, Qt Designer, Qwt, CMake and Ninja**. CMake compiles every Designer form with AUTOUIC; qmake and an application `.pro` file are unnecessary.

## Build and run in Qt Creator

Run `scripts/build.ps1` once to build Qwt against the installed Qt 6 kit and configure the DDC SDK. Then open the root `CMakeLists.txt` in Qt Creator, select the matching **Qt 6 MinGW 64-bit** kit, configure Release with Ninja in `out/build/desktop-local`, and select **`aerotrace_desktop`** as the Run target. The SES qualification build used Qt 6.11.1; select the Qt kit installed on the current build machine. Do not reuse `out/build/desktop-release`: its cache references another PC's Qt 6.12/Qwt paths. Rebuild Qwt and use a fresh build folder when changing Qt kits.

The Windows helpers default to the installed `C:\Qt` kit:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -PackageName AeroTrace1553-SES-Qt611-DDC315-Verified
```

Launch the installed application or the locally built `aerotrace_desktop` target. **Connect devices** opens/configures the selected DDC channels without acquisition or recording. In Live Monitor, **Start recording** begins receiving and saving. **Stop & save** finalizes the recording and paired CSVs while retaining the connection. **Disconnect devices** saves an active recording before releasing the card. Device arrival and application launch never start a recording automatically.

## Operator workflow

- **Device 0 / Channel 1 = External; Device 1 / Channel 2 = Internal.** A/B are redundant paths within each channel. Both channels and all four bus paths are enabled by default in the updated workflow. The approved RT grouping is implemented in the development source: fixed RT rows per channel, independent A/B values/counts, expandable routes and a chronological mode. Logical IDs remain configurable.
- **Both channels**, **Device 0 · External** and **Device 1 · Internal** tabs select the table view. Bus and RT filters affect the display, not raw recording.
- Collapse the sidebar with its chevron or **Ctrl+B**. Icons retain accessible names and tooltips. Eight primary destinations stay visible; **More tools** retains the other screens, including replay, inspector, exports, security and diagnostics.
- Recording always requires **Start recording**. Changing connection settings closes the idle connection so the new settings are applied on the next explicit Connect.
- Select a row once; it remains selected as new batches arrive. **Inspect**, **Engineering** and **Plot** give direct access to the existing analysis views.
- **Export CSV** saves a unique file, checksum and ICD sidecar into the configured export folder without a repeated Save dialog. It applies the selected display filters to the retained analysis window. Load a completed session for exports of its full loaded contents.
- Drop one local ICD **PDF or JSON** onto the workspace, or press **Ctrl+I**. Complete supported PDF rules activate automatically; uncertain extraction still requires evidence review. The active profile persists between launches.
- Body text is 15 px, the monitor table is 14 px with larger rows, and the palette uses neutral surfaces, graphite, green, amber and red. Blue and purple accents are removed, including plot traces.

The self-contained package includes Qt dependencies. A bare build executable should be run through the configured Qt Creator kit or with the Qt runtime on PATH.

See [operator design and reference review](docs/design/operator-workflow.md) and [validation evidence and remaining qualification](docs/validation.md).

## ICD, live engineering and card time

**ICD chat** in the header (**Ctrl+J**) opens the separate [ICD Assistant](docs/icd-assistant.md). Choose a PDF, index it, ask cited questions and return to its saved conversation later. Answers are scoped to the selected ICD; switching chat documents does not switch the active conversion rules. The Mappings and evidence tab provides explicit review/activation.

Use [the demo ICD kit](examples/demo-icd/README.md): a digital PDF, image-only OCR PDF, matching JSON and expected conversion CSV. The digital PDF automatically activates nine validated rules. The scanned sample exercises OCR and currently sends nine uncertain rows to evidence review; it does not silently activate questionable conversions. ICD understanding creates a local searchable index and validated rules, rather than retraining model weights.

Engineering displays all active mappings with raw words, engineering values, units, `HH:MM:SS.mmm.uuu` card time and quality. Select a row and Plot selected signal, or double-click it. Monitor Columns → Individual data words exposes all 32 word columns; double-click a mapped word for engineering plotting, or plot an unmapped numeric column as raw values. Signal plots and cursor inspection stay on the selected channel/bus clock. Hours do not wrap at 24. Original ticks and packets are preserved; physical idle gaps and clock resets are reported rather than concealed.

Up to eight plotted mappings from anywhere in the 512-rule profile retain worker-owned history independently of monitor-row eviction. Raw-column plots use the retained analysis window. Cursor inspection reports an evicted raw record rather than selecting unrelated newer data.

[Timing and diagnostics](docs/card-time-and-diagnostics.md) documents the receive-only DDC API sequence, precision, rollover and clock limitations. Capture JSONL logs include SDK results, periodic queue/loss/conversion/timing metrics and final state. Application/document logs rotate locally. Run **Collect Diagnostics.cmd** to collect driver/runtime inventory and recent logs for analysis.

## DDC hardware

The Windows adapter builds against the selected installed aceXtreme SDK, loads its matching 64-bit `emacepl.dll`, and selects legacy MT or MTI receive-only mode from device capabilities. It imports no BC/RT transmit functions. SDK logical device numbers are configurable separately for channels 1 and 2; each channel has bus A/B selection. Hardware is the default source; simulator traffic requires explicit selection.

The supplied November 2 SES notes report BU-67113U, started acexusb 3.6.15.1 and logical channels 0/1. The original zero-message check was followed by loopback: 6,041 messages over 600 seconds before clock correction, then six corrected-clock runs totaling 606 messages with populated CSV verification. These are historical results from SES, not live inventory of the computer reading this source. See `.codebase-memory/RecordingFix-validation-20261102.md`.

`DDC_SDK_ROOT` is a CMake cache path. The licensed SDK remains external to the source tree. The Windows package includes the selected runtime/license for this PC; external redistribution remains subject to the vendor license. Qt 6.11.1 and SDK 3.6.15 were tested here. Other Qt 6 and DDC SDK versions require a matching rebuild and validation; universal version compatibility is not established.

## Automatic ICD workflow

1. Open **ICD Library → PDF / OCR / ICD evidence**, or import a PDF directly.
2. Embedded text/tables are extracted first. Image-only pages use local OCR, including cell recognition for ruled tables.
3. Complete explicit rules become a validated profile automatically. Missing addressing, unclear indexing, unsupported types, conflicting IDs or uncertain OCR remain in review. Review candidates against their linked source page and check accepted rows; unresolved values never become live rules.
4. Approved mappings feed worker-side engineering conversion and live plots. Integer fields up to 64 bits, signed values, IEEE float32/64, packed BCD, scale/offset, word order, channel and bus constraints are supported.
5. Search imported ICDs using **SQLite FTS5 BM25 + local MiniLM semantic vectors**. Extractive answers include document/page citations. An optional loopback OpenAI-compatible model endpoint can synthesize prose; no language-model server or generative weights are bundled.

Document originals, hashes, page evidence, profiles and indexes stay in the per-user application data directory. Processing uses one cancellable child process. On Windows it has a 1 GiB commit limit, a requested 20% CPU hard cap, below-normal priority, two inference threads and a 15-minute job timeout. The document process never owns hardware state. Package setup downloads dependencies/models once; document import itself needs no network.

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
  acquisition/                One source/record/decode worker, DDC and simulator
  application/                Qt-free profile, replay, analysis and export services
tools/document_worker/        Isolated PDF/OCR/embedding/retrieval process
scripts/                      Build, setup, package and comparison tools
```

The capture worker owns SDK lifecycle, raw packet preservation, recording and conversion. It publishes batched records through an **8192-record display queue**; display backpressure skips the oldest display records, never the raw disk write. SDK loss and overflow stop and mark the session incomplete. Recording flushes at 100 ms and at close; a stream flush is not a power-loss durability guarantee.

Profiles contain at most 512 signals. Address routing avoids decoding every mapping against every record. History is bounded to 8192 valid samples per signal; up to eight traces publish at most 1024 min/max envelope points each. The raw table holds 5000 records; the analysis cache trims at 200000 records. Full raw data stays on disk. Large-session streaming/indexing and power-loss qualification remain release work.

Release builds use CMake's optimized flags and supported interprocedural optimization. The latest validation report records measured software throughput and GUI latency, separately from unperformed hardware qualification.

## Recordings, exports and checks

ATD2 preserves source, raw device ticks, tick units, host monotonic time, vendor block status, second command/status words and validity. ATD1 files remain readable. DDC sessions also retain original per-channel Chapter 10 packet sidecars, a profile snapshot and final integrity status. Replay orders by device timestamps. Hardware clock alignment across devices still requires bench calibration.

Profile changes are audited in `.icd.jsonl` with their first raw-record boundary; `.profile.json` retains the latest snapshot. Replay currently uses that snapshot. Automatic replay of every historical mapping revision remains a release task.

Exports include source provenance, time bounds, engineering units and SHA-256 sidecars. Engineering CSV uses the active multi-signal profile. Active-view exports use the bounded analysis cache; open a completed recording to export its full loaded contents.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -CoreOnly -Test
```

The backend builds independently without Qt/Qwt. GUI test modes are `--workflow-test`, `--smoke-test`, `--performance-test` and `--document-workflow-test`; CTest supplies their arguments. Headless checks use `QT_QPA_PLATFORM=offscreen`.

See `docs/validation.md`, `docs/implementation-status.md`, `docs/architecture.md` and `docs/compatibility-matrix.md`. RHEL/DDC integration, physical cryptographic licensing, full protocol reconstruction, representative-ICD acceptance, long-duration hardware load tests, installers/signing and production release gates remain explicitly unqualified.

## Recording/CSV correction

The session export uses exactly 42 columns matching the supplied DDC CSV examples, per-channel microsecond deltas, `HH:MM:SS mmm.uuu` card time, lowercase four-digit hex words, decoded Rx/Tx command strings, blank unavailable statuses, and `-` for unused words. Files are paired `_EXT.csv` / `_INT.csv`. Existing exports are never overwritten by a different recording; raw ATD/packet files and integrity sidecars are retained.

DDC MTI packet timestamps follow the selected native TTRES setting. The adapter requests `ACE_TT_2US` and preserves ticks with a 2000 ns resolution; it does not assume all DDC Chapter 10 packets use 100 ns. SDK Windows caller-buffer release returning `ACE_ERR_NOT_SUPPORTED` is a no-op only while the returned packet pointer remains the caller-owned buffer. Other release failures and SDK-owned packet failures remain capture errors.

## 0.5.5 installation

The installer requires administrator rights and installs in `C:\Program Files\AeroTrace1553`. Recordings, preferences, profiles and logs remain outside the application installation. Uninstall removes the installed runtime trees and their generated Python caches. Build and package tools are excluded from the source ZIP; run `scripts/build.ps1` to regenerate the local build.

### Local qualification and SDK providers

This development source includes [versioned SDK provider DLLs](docs/provider-contract.md) and [native Windows preflight, Qt model tests, sanitizers, fuzzing and a passive bench kit](docs/local-verification.md). No CI/CD workflows are configured. Run `scripts/preflight.ps1 -Profile Full` for local evidence; unavailable SDK packages are BLOCKED and physical qualification remains NOT RUN until bench acceptance.
