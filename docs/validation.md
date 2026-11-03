# Validation — 29 October 2026

Current operation and qualification status: [0.5.5 status](current-status.md). Older machine-specific results below are historical evidence; they do not describe current hardware presence or qualify later builds.


Windows x64 host, Qt 6.12.0, MinGW 13.1.0, Qwt 6.3.0, C++20 and CMake/Ninja Release. This records software evidence and outstanding acceptance work; it is not hardware qualification.

## 0.3.0 hardware-test delivery

The final application suite passes **17/17** checks in 46.55 seconds. This includes all prior capture, replay, conversion, PDF/OCR, plotting, automation and stress checks, plus structured concurrent logging, escaping/sequence, rotation, unwritable storage, an isolated native crash-dump test and support-bundle collection from a custom recording directory. Test preferences are isolated from normal settings and from installed program files.

Application logs now include per-run identity, UTC/elapsed time, process/thread IDs, actions, document-job progress/errors and batch/export outcomes. Capture logs carry the same run ID and a configuration/check snapshot. The support collector gathers current/recent journals and capture metadata without initializing devices. The new vector icon is rendered into a multiresolution ICO and embedded in the executable and interface. The nine-page [Operator guide](Operator-Guide.pdf) includes the first physical bench procedure and support evidence checklist.

Delivery uses a single offline installer containing dynamically linked Qt/Qwt and the isolated document runtime/models, including the required signed Microsoft x64 CRT redistributables. It is not a static DLL-free executable. The preview is unsigned. The static PE dependency audit has no unresolved non-system imports; runtime workflow checks remain necessary. Windows 10 1809+ / Windows 11 x64 are the targeted operating systems. Physical DDC qualification and a separate clean-PC hardware acceptance run remain pending.

## Earlier automation delivery

The updated desktop Release build passes all 15 CTest checks (51.12 seconds on the final run). New evidence covers saved workspaces and added traces, rejected invalid workspaces, reviewed action previews through the actual Python worker, C++ application of operator clarifications, stale-preview rejection, deterministic ICD payload simulation, shared-message packing, 100-hour simulator timestamp arithmetic, signal limit/hysteresis/missing-gap checks, persistent live check events, batch cancellation/partial failures, and complete engineering export beyond 24 hours.

The optional PIV gate is compiled and its missing-certificate path fails closed without a PIN prompt. Real key detection, signing, PIN/touch behavior and removal/reinsertion remain unverified: Windows did not enumerate a Yubico/PIV device during repeated read-only inventory checks. The delivered development build has enforcement disabled pending enrollment.

The eight original RTPro reference files match the investigation baseline hashes. Updated standalone delivery: `dist/AeroTrace1553-Automation/AeroTrace1553.exe`. See [task automation](task-automation.md) for operation, supported reviewed commands and current limits.

## Current hardware-test delivery

The ICD Assistant adds strict per-PDF retrieval, persisted per-document chat, grounded mapping answers and contextual follow-ups. Worker checks use two ICDs with conflicting TEMP units; GUI checks reopen the modeless window and verify conversation restoration without changing the active profile. The supplied nine-field demo also verifies conversion and plotted samples from a completed recording after live capture stops. The final Assistant Release suite passed **12/12**, 75.71 s (`out/validation/icd-assistant-release-final.log`). An earlier combined run timed out waiting for a PDF; its isolated recheck passed in 6.03 s, and the complete final suite passed without changing the deadline. See [ICD Assistant](icd-assistant.md).

Final subscription-aware Release suite: **12/12 passed**, 39.12 s (`out/validation/final-subscription-tests.log`). Final Qt-free suite: **7/7 passed**, 22.00 s (`qt-free-final.log`). The self-contained deployment passed workflow, operator, actual supplied demo-PDF and all 18 screen checks with only Windows directories on PATH (`deployed-final/result.txt`). Later fields have worker-owned histories through eight active subscriptions; field 512 and raw-window-independent ninth-field history are covered. Cursor eviction and the numeric delta column also have regression checks.

Final synthetic stress (`hardware-ready-performance.json`): 153,288 CRC-valid records; requested 50,000 events/s; 3.335 s; zero display skips; queue high-water 6,774/8,192; maximum GUI heartbeat gap 46 ms; maximum batch 40.8703 ms; four plots; at most 1,024 points/trace. These are short synthetic observations, not a hardware-rate guarantee. Earlier measurements below are retained separately.

Final native startup was observed with the deployed executable. Both selected devices reported their actual registry initialization error -80; the JSONL captured the vendor operation, logical ID, channel, exact error and zero-record failed closure. The verification instance was then closed. The packaged diagnostics collector ran with its default application-directory resolution and produced driver-store/runtime inventory. Deployment log: `out/validation/deployed-app-check.log`; collector evidence: `out/validation/deployed-diagnostics`.

- Release CTest **12/12 passed**, 39.93 s (`out/validation/hardware-ready-final.log`). Qt-free backend **7/7 passed** (`out/validation/hardware-ready-core.log`). Subsequent targeted checks cover the final counter-wrap corner case and updated demo/visual harness (`rollover-tests.log`, `demo-visual-tests.log`, `final-visual-tests.log`).
- Supplied digital demo PDF was compared field-by-field with its nine-rule JSON profile and exercised cited retrieval. The actual image-only PDF was processed through OCR: nine rows require review and remain inactive. Evidence: `out/validation/demo-icd-verification.json`.
- Live GUI integration covers the ninth mapping on Channel 2 / Bus B, every engineering row's raw/value/unit/card-time/quality fields, mapped-word and raw-column plot selection, and cursor resolution to the correct original stream. Demonstration captures/rendering use clearly labeled simulator traffic.
- Timestamp checks cover microseconds, more than 24/100 hours, unused intra-packet bits, 48-bit rollover, a delayed pre-wrap message after rollover, explicit unsupported time formats and independent source histories. The adapter preserves original ticks and packets. These are software checks, not card-clock accuracy qualification.
- New capture logs record API results, device state, profile changes, loss/overflow, queue pressure, conversions and per-stream clocks; application/document logs and a diagnostics collector were exercised. Actual inventory confirms signed DDC PCI/USB driver-store packages 4.10.2.1. No driver installation or vendor assignment was changed. Runtime API reports 4.10.2; the DLL's file-version resource reports 4.10.1.1. Both real logical-device initializations still return -80.
- Latest standalone synthetic stress: 153,699 CRC-valid records, requested 50,000 events/s, 3.361 s elapsed, zero display skips, queue high-water 5,514/8,192, maximum GUI heartbeat gap 31 ms, maximum worker batch 45.0642 ms, four plots and at most 1,024 points/trace (`hardware-ready-performance.json`). This short observation is not a sustained hardware-rate guarantee. Other loaded runs below show display thinning.
- The updated demo visual test runs at 120 synthetic events/s for readable operator plots. The separate three-rule PDF/conversion test retains 50,000 events/s stress. `docs/design/live-engineering.png` and `live-plots.png` are actual Qt captures during recording, not generated UI art.

See `docs/card-time-and-diagnostics.md` for precise clock/log semantics and `examples/demo-icd/README.md` for the user workflow. Physical capture, clock accuracy/synchronization, disconnect/overflow and sustained-load qualification remain for the hardware test.

## Earlier operator redesign verification

- Final desktop Release build and CTest: **11/11 passed**, 55.57 s (`out/validation/redesign-release-tests.log`). Independent Qt-free backend: **7/7 passed** (`out/validation/redesign-core-tests.log`).
- The new operator check covers actual table-width growth on collapse, accessible icons, External/Internal device isolation, combined device/bus filters, workspace height, real one-click CSV output, stable live selection and Stop hold across configuration/arrival retries.
- Acquisition checks now include delayed asynchronous source initialization and cancellation. The test DDC SDK requires both devices configured before any start, and proves a second-device configuration failure starts neither producer and creates no raw recording.
- All 18 updated pages rendered to `out/validation/redesign-screens`. The native self-contained application was inspected maximized at 1366×720, with expanded/collapsed navigation, correct device tabs and all ten secondary tools in the native menu. Both real devices showed readiness failures rather than invented traffic. The temporary verification instance was closed before repackaging.
- Latest synthetic stress: 155,911 CRC-valid records, requested 50,000 events/s, 3.736 s elapsed, 1,123 skipped display copies, bounded queue high-water 8,192/8,192, maximum GUI heartbeat gap 60 ms, maximum worker batch 18.8772 ms, four plots and at most 1,024 points/trace. Results: `out/validation/redesign-performance.json`. Earlier zero-display-skip runs are separate observations, not a sustained-rate guarantee.
- Concurrent PDF conversion: 646,520 recorded events, 6,617 display skips, queue high-water 8,192. Capture, automatic activation of three mappings, cited retrieval, worker engineering conversion and live plotting passed. The presentation queue may thin rows under contention; raw recording remains separate. Evidence: `out/validation/redesign-document-workflow.txt`.
- Design decisions, sampled video times, feature inventory and the concept/implementation comparison are in `docs/design/operator-workflow.md`. All 24 Designer forms remain editable; the stack remains C++20/Qt Widgets/Qwt/CMake.

## Earlier build and execution evidence

- Fresh desktop Release CTest: **10/10 passed** (`out/validation/build-final.log`).
- Independent Qt-free backend build: **7/7 passed** (`out/validation/core-final.log`). Its document tests use the isolated Python runtime; the C++ libraries do not link Qt or Qwt.
- Qt Creator opened the root CMake project with the Qt 6.12 MinGW Release kit. Ctrl+R built and launched `out/build/desktop-release/apps/desktop/aerotrace_desktop.exe`; the native application window and its DDC/local-workspace gate were observed. The selected run target is the desktop application.
- All 18 page bodies, shared chrome and document dialog compile from 24 Designer `.ui` files. The canonical deployed application is `dist/AeroTrace1553/AeroTrace1553.exe`.
- `monitor_page.ui` opened in Qt Creator's native Design mode; Widget Box, form canvas and Property Editor were observed. No form edit was needed for this check.

## Automated checks

| Test | Evidence |
|---|---|
| `offline_pdf_ocr_and_retrieval` | Digital and rasterized PDF ingestion, table parsing, OCR evidence, ambiguous indexing/confidence handling, unsafe expression rejection, local embeddings and cited retrieval. |
| `icd_pdf_fixture` | Generates the explicit ICD PDF used by GUI integration. |
| `gui_pdf_to_live_conversion` | Capture starts before PDF ingestion. Three validated rules activate automatically; a cited unit answer, worker conversion and Qwt samples are verified while capture continues. |
| `core_contract_record_replay` | Command/decode boundaries, round trip, CRC, truncated-tail recovery and malformed-header rejection. |
| `ddc_adapter_contract` | Test SDK exercises MTI mode, batching, raw packet preservation, hardware metadata, loss fail-stop and missing-runtime errors. Test DLL is not packaged. |
| `acquisition_recording` | Source filtering, recorded/delivered correspondence, stop/restart and file failure. |
| `bounded_pipeline_and_conversions` | Stalled display, queue bounds, raw preservation, batching, signed/multiword/float/BCD conversion, profiles, downsampling and profile revisions. |
| `application_services` | Qt-free replay, seek/clock, series, export bounds/escaping; unloaded replay cannot show live data. |
| `gui_workflows` | 18-page navigation/scrolling, gate access, explicit simulation, capture, pause independence, closure, filters, replay/seek and lock. |
| `gui_live_pipeline_stress` | Four plots, queue bounds, worker timings, GUI heartbeat, downsample cap and CRC-valid synthetic recording. |

Measured standalone GUI stress (`out/validation/performance-measured-20261029.json`): requested 50,000 synthetic events/s, 153,929 records, zero display skips, queue high-water 7,798/8,192, maximum GUI heartbeat gap 68 ms, maximum worker batch 39.134 ms, at most 1,024 points/trace and CRC-valid raw data. This short run is not a guaranteed sustained hardware rate.

Concurrent PDF/capture (`out/validation/document-measured-20261029.txt`) recorded 441,301 records and reported 750 skipped display copies with high-water 8,192. Other loaded and packaged runs reported more display skips. These are separate from raw-record loss; bounded presentation may thin rows under contention. The stalled-display contract retains all 100,000 raw records while its deliberately tiny 64-record display queue discards 99,936 display copies.

## Installed hardware evidence

Installed SDK: `C:/DDC/aceXtremeSDKv4.10.2`. Its x64 `emacepl.dll` loads and reports API 4.10.2. Driver-store inventory includes signed DDC PCI/USB packages at 4.10.2.1. No present DDC device was returned by the local inventory.

The earlier receive-only probe of logical device 0 and the redesigned packaged startup for **both logical devices 0 and 1** return **Unable to open Win32 registry key (-80)**. Card connection and vendor logical-device assignment are required before physical capture can be verified. The adapter has no BC/RT transmit entry points; that source property does not establish electrical transmit inhibit.

## Deployment and visuals

The package includes Qt/Qwt, the installed DDC runtime, isolated Python, OCR and quantized MiniLM assets, pinned dependencies and license notices. Packaged health, PDF-to-live conversion and 18-screen rendering passed with only Windows directories on PATH. Import/retrieval use local models and content. Optional generation requires a separately configured loopback model server.

Earlier renders are under `out/validation/final-screens`; updated renders are under `out/validation/redesign-screens`. `scripts/compare-screens.py` compares immutable original references without masking/resizing. The operator redesign deliberately supersedes the earlier blue/purple screenshot styling and startup sequence. All 18 page bodies remain real editable widgets. Exact visual acceptance and comprehensive accessibility/high-DPI validation are not claimed.

Capture owns `.profile.json` and `.icd.jsonl` sidecars. The timeline records revisions and their first raw-record boundary. Replay loads the latest profile snapshot; automatic reconstruction using every historical revision remains work. Raw data remains available for reprocessing.

## Remaining acceptance gates

Physical DDC connection/configuration, actual disconnect/overflow behavior, sustained/burst traffic, disk stalls, cross-device clocks, representative customer ICDs, full transaction semantics, alarm/statistics coverage, physical cryptographic key enforcement, huge-session streaming/indexing, rotation, fsync/power-loss recovery, RHEL, signing and installer qualification remain open. Autonomous model training is not implemented. Default document answers are cited extracts; no generative weights/server are bundled or qualified.

Old generated artifacts remain in the sibling `aerotrace-cleanup-20261028` archive. Redundant migration scripts were removed. A duplicated generated document-worker directory was moved into `out/validation/package-duplicate-worker-20261029`. Source, references, licenses and existing recordings were preserved.
