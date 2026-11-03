# Implementation and qualification status — 2 November 2026

See [current status](current-status.md) and [compatibility matrix](compatibility-matrix.md). The supplied 0.5.5 SES release reports 21 tests and limited A-path loopback; expanded physical qualification is the first remaining gate.

## Implemented

- Approved External/Internal side-by-side RT grouping, persistent parent rows, independent A/B latest values/counts, expandable message routes, latest-word previews, native keyboard selection and display-only pause. Receive-side aggregates remain independent of raw display thinning; chronological views remain accessible.

- Eight active worker-owned trace subscriptions can target any of the 512 validated mappings. Later/slow signals retain plot history independently of monitor-row eviction. Cursor inspection snaps to real plotted samples and refuses to associate evicted historical points with newer raw data.

- Nine-rule digital/demo OCR inputs, JSON parity and expected vectors; actual digital ingestion/retrieval and live GUI conversion/plotting verified, including the ninth mapping on Channel 2 / Bus B. Scanned sample is indexed through OCR and retains nine rows for confidence review.
- All active engineering rows expose raw words, units, card time and quality. Numeric monitor/word selection drives mapped engineering or raw plotting, with per-stream history and cursor isolation.
- Microsecond card-time formatting beyond 24 hours, 48-bit relative timestamp handling/rollover, explicit unsupported-format errors, per-stream discontinuity statistics, SDK/health/profile/application/document logging and driver diagnostics collection.

- Direct monitor startup, asynchronous selected-device checks, configure-all-before-start behavior, explicit connection/recording separation, cancellation and retained connections.
- User-confirmed External/Internal device tabs, full workspace table, collapsible accessible icon navigation, retained row selection, configurable raw columns, quick CSV and PDF/JSON drop import.
- Neutral graphite/green operator theme with larger typography; all 18 original screens remain reachable.

- All 18 screen bodies and shared layouts are Qt Designer forms; the document center is also Designer-editable.
- Separate Qt-free core, acquisition and application libraries; desktop form controllers and strict profile JSON adapters.
- aceXtreme integration using dynamically loaded 64-bit legacy MT or MTI receive-only APIs, configurable logical channels/buses, packet batches, device/host timestamp provenance and original Chapter 10 sidecars.
- Single capture/record/decode owner, bounded display queue, separate display-skip/hardware-loss counters, buffered recording, final SDK drain and incomplete-session status on errors.
- Multi-signal conversion: integer bit fields, signed values, float32/64, BCD, word order, scale/offset and channel/bus matching. Invalid/flagged data does not produce a valid conversion.
- Worker-side histories and min/max plot downsampling; bounded GUI model and analysis cache.
- Digital PDF table extraction, local English OCR and ruled-cell recognition, source hashes/pages, automatic activation of complete explicit mappings, and checked review candidates for uncertain OCR.
- Local FTS5 and MiniLM semantic indexing, cited extractive answers, optional loopback model endpoint.
- Separate modeless ICD Assistant (header / Ctrl+J), persistent per-document conversations, strict document-scoped retrieval, concise mapping answers and contextual follow-ups. Explicit action previews can apply reviewed conversion clarifications and plot selection, with stale-profile checks and operator provenance. Ordinary questions do not change conversion rules. See [ICD Assistant](icd-assistant.md).
- Saved task workspaces, applied signal checks, reproducible ICD-based payload simulation, sequential batch engineering CSV/JSON/HTML reports and cancellation. See [task automation](task-automation.md) for limits and validation.
- Real local recording/replay, filters/inspection, profile JSON, multi-signal engineering CSV, raw CSV/JSON, checksums, plot export and preferences.
- CMake Release builds, supported IPO, package helpers, native MCP graph tools, software pipeline/GUI/document tests.

## Evidence and remaining gates

| Area | Current evidence / limitation |
|---|---|
| DDC runtime | SES qualification used 3.6.15; older development host used 4.10.2. Other compatible receive interfaces can be discovered; ABI and hardware must be validated. |
| DDC drivers | Supplied SES evidence: started acexusb 3.6.15.1. Inventory must be obtained on the actual test machine. |
| Physical capture | Limited A-path loopback passed on SES. Responding RTs, B, simultaneous channels, endurance and reconnect remain unqualified. |
| Adapter behavior | Test SDK exercises MTI mode, batches, metadata, packet files, loss fail-stop and missing-runtime handling. It does not qualify vendor/USB timing. |
| Performance | Automated stalled-display and GUI stress tests run against software sources. Hardware sustained/burst rates, disk stalls and simultaneous OCR must be measured on the real bench. |
| Document coverage | Representative ICD PDFs have not been supplied. Explicit supported schemas are tested; arbitrary layouts, handwritten scans, omitted indexing, enums and unsupported formulas require review/adapters. |
| Generative answers | Loopback API integration exists. No generative language-model server/weights are bundled or qualified. Default cited answers are extractive. |
| Learning | Persisted preferences, active profiles and document indexes exist. Autonomous training or automatic rule changes from model answers are not implemented. |
| Licensing | Optional Windows PIV challenge/signature gate implemented with a pinned release-build certificate and periodic verification. Enforcement is disabled in the development build; compatible physical key enumeration, enrollment and removal/reinsertion acceptance remain pending. No Sentinel entitlement is provided. |
| Protocol | Command fields and SDK-decoded records are present. Complete BC↔RT/RT↔RT transaction/advanced error semantics still need qualification. |
| Storage | ATD2 development format and original DDC sidecars; final chunk/index/rotation, fsync/power-loss behavior and huge-session streaming remain work. |
| Analysis | Engineering limits/hysteresis, per-stream missing timers, protocol/status/clock transitions and batch signal statistics are implemented. Physical qualification, broader timing analytics and advanced topology remain acceptance work. |
| Visuals | Real Designer controls follow references. Exact pixel identity and full accessibility/high-DPI acceptance are not established. |
| Platforms/release | Local Windows build/package checks; RHEL, optional physical key, signing and external redistribution approval remain pending; supplied 0.5.5 machine install/uninstall was verified on SES. |

The executable is a hardware-oriented application with a separate simulator source. Neither its name nor software tests establish production qualification.
