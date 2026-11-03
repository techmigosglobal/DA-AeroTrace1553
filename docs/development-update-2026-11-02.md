# Development update — 2 November 2026

The user approved the side-by-side RT layout during this work. The HTML/SVG proposal remains a design artifact; native Qt implementation and test captures are separate.

## Implemented

- Receive-side RT aggregates independent of the display queue: stable per-channel RT parent rows; separate A/B latest records and totals; bounded message-route summaries; RT-to-RT participants, broadcast and unclassified traffic.
- Native Designer-backed expandable RT panes, latest-word previews, per-pane display filters, display-only pause and a chronological mode. New captures reset aggregate identity; profile changes do not erase RT activity.
- Complete SDK receive-interface reports, including packet-release and integrity functions; explicit missing-symbol diagnostics. Fixture coverage for several API version identifiers, all 15 channel/bus masks and reconnect after injected faults. This is not universal vendor ABI qualification.
- Passive hardware qualification collector and a matrix for responding RTs, B paths, simultaneous channels, endurance, repeated recording and physical reconnect.
- Unified structured application logging with severity and Qt source/category context; SDK connection outcomes, ABI-size evidence and document-process startup diagnostics.
- Event list/export retrieval and RT-to-RT engineering export/batch statistics corrections.
- Replay load-request generation guards and explicit pending-load/seek synchronization in GUI regressions.
- Document dialog shutdown disconnects process callbacks before widgets are destroyed. Window teardown explicitly closes the dialog while application state still exists.
- Current-status/compatibility/hardware/logging guides reconciled; historical results labeled; offline Operator Guide HTML/PDF rebuilt.

## Verification history

The first full development run passed 21/23 checks. The demo ICD test timed out during ingestion and crashed during teardown of an active document process; the source showed callbacks into form widgets during QProcess destruction. The session test could check data before a queued asynchronous replay load/seek completed. Shutdown protection and replay synchronization were implemented; final results are recorded below after verification. The initial document startup delay itself is not treated as a diagnosed vendor/runtime fault.

No DDC card was returned by current Windows present-device inventory. Physical qualification has not been performed on this computer. Supplied SES loopback evidence is historical and remains limited to its recorded configuration.

## Remaining stages

SQLite session metadata, a measured DuckDB/Parquet integration, isolated NumPy/SciPy analysis, optional Tracy profiling, sanitizer/fuzz build configurations and representative-ICD corpus acceptance remain planned work. They are not claimed as implemented dependencies by this update. Physical qualification is the first release gate.

## Build and final results

Validation build: C:/Users/Vinay/AppData/Local/Temp/AeroTrace-development-20261102. Qt 6.12.0 MinGW x64, GNU 13.1.0, installed SDK 4.10.2, Release with IPO disabled. This isolated build uses the native plot fallback; a Qt-matched Qwt deployment still needs its own package/bench acceptance. No installer was replaced.

Final verification completed 3 November 2026. The full 25-test run passed 24 checks; the older operator workflow left its row mode unset, so the new first-use RT default replaced that test's requested tabbed view. The test now explicitly selects chronological mode. Its targeted rerun passed 1/1 in 3.99 seconds. All 25 checks have therefore passed across the full run and the corrected targeted rerun; this is not described as a single 25/25 run.

Coverage includes the RT aggregate and native RT workflow, SDK discovery/contracts and all stream masks, passive qualification fixture, structured logging/crash collection, core acquisition/recording/replay, exports, demo ICD ingestion, session saving/layout and simulated hardware connection. Native GUI checks used Qt's offscreen platform. The [native RT screenshot](design/rt-live-monitor-implemented.png) contains simulated traffic. Per-workflow reports remain in test-results; Testing/Temporary/LastTest.log contains the latest targeted rerun.

The RT GUI regression checks independent A/B/channel values, stable row identity/selection/expansion, retention through display thinning, bus-specific inspection, recording during display pause and chronological view availability. It also exercises cancellation of an active document dialog.

## Per-PC SDK follow-up — 3 November

Device Setup now accepts an SDK folder as well as an exact DLL. DDC_SDK_ROOT is an explicit default ahead of automatic installed-SDK discovery and packaged/build copies; missing or invalid configured roots do not silently fall back. Fixture tests cover three directory layouts, configuration precedence and conflicting receive interfaces. After rebuilding, ddc_adapter_contract, qualification_four_stream_fixture, gui_rt_monitor_workflow and gui_hardware_connection_recording passed 4/4 in 18.93 seconds. This does not add ABI-family providers or physical compatibility evidence. The [readiness audit](development-readiness-2026-11-03.md) records the actual status of the remaining proposed technologies and performance limits.
