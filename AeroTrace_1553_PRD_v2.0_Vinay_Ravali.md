# AeroTrace 1553 — Comprehensive Product Requirements Document

**2 November 2026 precedence:** [Current behavior/evidence](docs/current-status.md) and [hardware qualification](docs/SES-Bench-Checklist.md) supersede older startup, SDK, installer and no-bus assumptions below. Connect configures only; Start recording is explicit. The user approved the side-by-side RT grouping; development source implements it. Earlier roadmap/licensing requirements are historical planning, not current implementation claims.

**Version:** 2.4 | **Updated:** 2026-10-29 | **Status:** Operator workflow and scoped ICD Assistant implemented; physical-card and release qualification pending  
**Development organization:** Two independently buildable modules — Vinay (Qt desktop + device/security adapters) and Ravali (Qt-free offline data core); frozen contract and controlled integration  
**Approved UI stack:** C++20 + Qt 6 Widgets + Qt Designer `.ui` forms + Qwt; CMake remains the authoritative build system  
**Target hardware:** DDC aceXtreme-compatible dual-channel hardware; supplied reference material names BU-67103U. The exact fitted SKU, firmware and MTI capability must be verified.  
**Platforms:** Windows 10/11 x64 and Red Hat Enterprise Linux x86_64 (exact releases/kernel combinations require validation)  
**Primary application mode:** Read-only passive Bus Monitor (MT/BM) with safe transmit-inhibit  
**Distribution:** Separate offline binary/package for each OS; licensing requires a separate cryptographic USB dongle  
**Data handling:** Local-only by default; no cloud connection or analytics telemetry

---

## 29 October operator workflow revision

The separate modeless ICD Assistant is available from the header or Ctrl+J. It stores per-document conversations, scopes every interactive answer to the selected ICD, resolves grounded mapping follow-ups, restores indexed documents without re-upload and separates chat selection from conversion-profile activation. Chat is an evidence/learning interface; mathematical conversion remains governed by validated or explicitly reviewed rules. The same profile can be applied during capture or to a completed recording. See `docs/icd-assistant.md`.

This revision supersedes the earlier screenshot styling and startup click sequence. The current user requests a professional, less textual interface without blue or purple, while retaining the existing C++/Qt features. Original reference screens remain a feature inventory; they are no longer a pixel-matching target. The two supplied September videos guide the device tabs, large table, inspection and CSV workflow. Documents from the separate Rust/IUP project are reference evidence and do not authorize a framework migration or establish current implementation coverage.

- Start directly in a maximized Live Bus Monitor. Check saved logical devices on the capture worker; never wait on SDK initialization in the GUI thread.
- User-confirmed identity: Device 0 / Channel 1 / External; Device 1 / Channel 2 / Internal. A/B are separate paths within each channel. Configure both selected channels before starting either producer, then wait for explicit Start recording. Defaults enable 1A, 1B, 2A and 2B.
- Display each device's readiness and errors separately. A loaded DLL is not a connected device. Monitoring state does not prove physical wiring, RT response or clock alignment.
- Stop holds capture stopped. Explicit resume creates a new session. Device-arrival notifications may retry selected-device initialization while not held stopped. Simulator data never starts automatically.
- Preserve all 18 existing pages through eight primary navigation destinations and a More tools menu. Sidebar collapse retains icons, tooltips and accessible names.
- Keep a large virtualized message table with Both channels / Device 0 / Device 1 tabs. Larger typography, configurable columns, retained row selection and direct inspection/engineering/plot actions reduce repeated navigation.
- Provide one-click filtered CSV export with unique filenames and integrity/profile sidecars. Its scope is the retained analysis window; full loaded sessions remain available through the existing session/export workflow.
- Allow a local PDF or JSON drop and Ctrl+I import. Preserve OCR, local retrieval, transactional profile validation, automatic activation of complete rules, review of uncertain rules and persistent profiles.
- Retain one acquisition/record/decode owner, bounded record and plot publication, batched writes, min/max downsampling, isolated document processing and optimized Release builds.

New visual reference: `docs/design/monitor-concept.png`. Implementation and evidence: `docs/design/operator-workflow.md` and `docs/validation.md`. Hardware qualification, representative ICD acceptance and other previously declared release gates remain required.

The hardware-test delivery adds a nine-rule demonstration ICD input kit, all-mapping live engineering rows with raw fields/time/quality, selectable per-stream engineering and raw-column plots, and `HH:MM:SS.mmm.uuu` display without midnight wrapping. DDC relative timestamps preserve 48-bit counter semantics, recognized rollover and original raw ticks. Independent device clocks are not merged into a fictitious synchronized plot. Clock discontinuities and actual intervals remain observable. Per-session API/health/conversion/timing JSONL and rotating application/document logs support bench analysis; a read-only diagnostics collector includes installed-driver inventory. See `docs/card-time-and-diagnostics.md`. ICD ingestion means indexing plus validated rules, not autonomous weight training.

---

## 1. Executive summary

AeroTrace is a professional, low-overhead, offline diagnostic instrument for observing MIL-STD-1553 traffic through the BU-67113Ux-family USB interface. It captures actual bus-monitor records (including command, status, payload words, time metadata, channel/bus identity, and available error/status flags), persists evidence-quality raw traces, decodes standard MIL-STD-1553 framing, maps payload fields through a user-supplied ICD/profile into engineering units, and presents live tables, charts, timelines, statistics, search, and replay.

The application is *monitor-only* in v1. It must never configure the DDC card to transmit BC or RT messages while monitoring an operational bus. It must provide explicit transmit-inhibit safeguards when the hardware/driver exposes them. The UI must never conflate an inferred logical transaction with directly captured physical bytes/words. Connection to an authorized training or test setup is required before hardware testing.

### 1.1 Product value proposition

- One unified workspace for bus presence, traffic, engineering signals, quality, alarms, and replay.
- Fast capture-to-disk independent from slow rendering or complex decoding.
- Portable, offline operation in network-isolated Windows and RHEL installations.
- Hardware-bound licensing using a second USB dongle, no online activation required.
- Realistic memory budget, deterministic bounded queues, measurable loss reporting, and reproducible tests.
- Configurable ICDs: no hard-coded avionics/system-specific parameter assumptions.

### 1.2 Non-goals, v1

- Acting as an active Bus Controller or Remote Terminal; transmitting operational traffic.
- Updating DDC firmware or performing unsupported electrical line diagnostics.
- Inferring engineering semantics from arbitrary hex bytes without a trusted ICD.
- Guaranteeing zero loss regardless of USB errors, host scheduler, vendor driver, or slow disk.
- Promising a single binary that runs on both Windows and Linux without OS dependencies.
- Cloud upload, remote control, network license servers, or Internet license validation.

## 2. Evidence, dependencies, and open hardware questions

DDC's official BU-67113Ux product page lists up to two dual-redundant 1553 channels, BC/MT and multi-RT/MT configurations, transmit inhibit, on-board DMA, 48-bit/100-ns timestamps, IRIG-B input, and the AceXtreme C SDK. The exact feature configuration, firmware revision and revision suffix of the user's device must be confirmed by DDC SDK interrogation, not inferred from the part-number label alone.

**Qualification gate HW-01:** Confirm device serial number (record locally but do not use it as a substitute for secure licensing), channel count and per-channel identity, bus A/B monitor coverage, firmware release, and revision.

**HW-02:** Obtain authorized vendor BU-69092 SDK media (headers, libraries, monitor-mode samples, license terms) for **both** Windows and the required RHEL release/kernel. DDC lists Windows 10/11 packages and Linux 5.x/6.x/7.x packages; distribution-specific support and USB module build must be tested.

**HW-03:** Confirm advanced monitor-record structure and whether hardware-origin timestamps, parity/sync errors, message-level statuses, gaps, lost-record counts, and IRIG-B correlation are exposed for this variant.

**HW-04:** Test startup in hardware monitor-only mode, verify transmission is inhibited, and ensure error/reconnect workflows never activate BC/RT transmission.

**HW-05:** Verify DDC SDK library linking/distribution: some vendor DLLs/.so files or kernel modules may remain external despite static linking of application-owned code.

**HW-06:** Document bench connector, termination, transformer/direct coupling and channel wiring from the approved DDC manual. Do not let the software presume an electrical connection is safe.

**Source:** https://www.ddc-web.com/en/connectivity/databus/milstd1553-1/boardsandboxes-1/bu-67113ux?partNumber=BU-67113

## 3. Stakeholders and user roles

- **Bus Test Engineer:** sets up device, starts capture, filters traffic, checks status and anomalies.
- **Data Analysis Engineer:** loads ICD, inspects decoded parameters, plots, overlays, computes ranges, exports results.
- **QA / Validation Engineer:** reproduces sessions, bookmarks suspect timestamps, exports evidence bundles and diagnostic logs.
- **License Administrator:** provisions secure key, checks feature grants and key validity without accessing content.
- **Software Maintainer:** validates driver matrix, updates signed application packages, reviews performance diagnostics.

Default app operation is local, single-operator. The licensing dongle proves product entitlement, **not user identity**. Operator login/audit identity is an optional separate requirement.

## 4. Primary workflows and acceptance criteria

### WF-01 Initial launch
1. Launch platform-specific AeroTrace binary.
2. Before enabling capture/UI workspace, locate approved physical dongle.
3. Perform cryptographic challenge-response and check product feature ID/validity.
4. If unavailable/invalid, show locked screen, clear reason, Retry, and offline help; never open previous raw data in unlocked analysis mode.
5. If valid, open Welcome/Recent Sessions dashboard.
**Acceptance:** Key absent -> no access to capture, replay, plot, ICD data, or exports. No Internet required.

### WF-02 Connect DDC monitor
1. Device discovery lists present DDC interfaces through the official SDK.
2. Show actual serial, firmware, driver version, channel count, per-channel settings.
3. Offer channel(s) and Bus A/B selection; name buses clearly as physical buses on each channel.
4. Set monitor-only, verify transmit inhibit if supported, read timestamp source, perform noninvasive health check.
5. Operator clicks Start Monitor; software displays armed/capturing state and diagnostic results.
**Acceptance:** No transmit configurations used. Failure paths show technical reason and recovery guidance. SDK calls are off the UI thread.

### WF-03 Live monitoring
1. Collect batches of raw SDK monitor events from every enabled channel.
2. Attach immutable origin metadata and detect hardware/host overflow.
3. Append capture bytes/words to the binary recorder before downstream filtering.
4. Normalize word and message representations; support incomplete/error records.
5. Publish limited-rate updates to UI table, counters, and live signals.
6. Operator may filter by channel, bus, RT, direction, SA, word type, status and error only for display; raw archival remains unfiltered unless an explicitly named filtered-recording mode is selected in a future release.
**Acceptance:** UI freeze or plotting overload cannot stall SDK event draining; resource overload flags visible loss instead of hiding it.

### WF-04 Load ICD and engineering decode
1. Open ICD Manager; import validated JSON/CSV mapping or construct signals through editor.
2. Validate structural uniqueness, RT/SA/direction patterns, word indices, bit ordering, scale and offset, units and validity conditions.
3. Preview decoded values against a selected raw record.
4. Apply versioned profile to live/session stream without altering recorded raw words.
5. Distinguish no-match, invalid, out-of-range, stale, and valid values.
**Acceptance:** For known synthetic vectors, raw/decoded values match golden test cases byte-for-byte and unit-by-unit.

### WF-05 Plot engineering values
1. Pick up to eight simultaneous signals per tab initially (configurable target).
2. Configure units, color, panel arrangement and time window.
3. Plot with original hardware timestamp and decimated display sample set.
4. Provide cursor A/B, delta time, delta amplitude, peak, min/max, zoom, pan, pause, auto-follow, screenshots/CSV export.
5. Pause plotting without stopping capture/recording.
**Acceptance:** Chart refresh does not exceed target rate; raw recorder retains all received records independent of plotted resolution.

### WF-06 Record, stop and replay
1. New Session -> choose location, label, optional profile, recording policy and disk free-space threshold.
2. Start capture writes to append-only versioned chunks and sidecar/session index.
3. Stop -> close current chunk, persist manifest/checksum/index, fsync policy, report duration and any loss intervals.
4. On power loss, recovery scans validated chunks and marks recording as recovered/incomplete.
5. Open existing session offline; seek by timestamp, frame number or bookmark; replay at 0.25x/1x/2x/5x or step frame-by-frame.
**Acceptance:** Replaying a session reproduces identical normalized raw records and engineering decode given the same ICD revision.

### WF-07 Analyze error / event
1. Detect supported parity/sync/timeout/invalid response/status flags using vendor metadata and parser rules.
2. Record event type, source evidence and confidence of derived interpretation.
3. Show severity and counts with trace navigation; events link to exact raw time and bus/channel.
4. Support acknowledgement/bookmark/annotation but do not delete evidence.
**Acceptance:** All alerts are reproducible on replay; alarms relying on inferred timing are explicitly labeled as derived.

### WF-08 USB dongle hot removal
1. Detect missing/invalid key at monitored interval and on privileged action.
2. Mark UI LOCKING; stop hardware acquisition as soon as practical; disallow further actions.
3. Flush queued raw records, finalize or recoverably close file, release device handles.
4. Replace workspace with lock screen; existing already-written files remain intact, no secret key data logged.
5. Replug -> perform a new challenge-response; require deliberate operator re-arm to resume.
**Acceptance:** No active capture continues after license failure; all preserved data is labeled with the interruption time.

## 5. Functional requirements, priority and IDs

**P0 — Release-blocking**

- **LIC-001**: local physical dongle required at startup; verify cryptographic response, entitlement and revocation/expiry settings if provisioned.
- **LIC-002**: continuous presence validation and fail-closed capture stop on removal; crash-safe finalization.
- **LIC-003**: no hard-coded universal key or validation based solely on VID/PID/USB serial.
- **DEV-001**: enumerate BU-67113Ux devices, SDK/driver/firmware versions, channel count and supported features.
- **DEV-002**: explicit monitor-only, receive-only setup and verified Tx inhibit where available.
- **DEV-003**: independent channel and physical bus A/B identity preserved in every raw record.
- **CAP-001**: batch capture, vendor/hardware timestamp preservation, host monotonic timestamp as secondary value.
- **CAP-002**: fixed-memory queues, raw write-priority, overflow/error counters and truthful recording state.
- **CAP-003**: proper 1553 command/status/data framing and BC-to-RT, RT-to-BC, RT-to-RT, broadcast/mode-command support.
- **CAP-004**: preserve malformed or partial record data when exposed by SDK; tag integrity status.
- **ICD-001**: configurable mapping from RT, SA, direction, message shape and payload words to named signals.
- **ICD-002**: support unsigned, signed two's complement, bit/enum, BCD, configurable multiword layouts, linear scale, offset and unit.
- **ICD-003**: show raw hexadecimal adjacent to interpreted values; decoding failure never fabricates a value.
- **REC-001**: loss-aware local capture format with versioned headers, checksums and recovery.
- **REC-002**: searchable replay with deterministic event order and exact raw reproduction.
- **UI-001**: responsive live message table, hardware/license status and acquisition controls.
- **UI-002**: plot workspace, eight signals per view, configurable panels, 10–30 FPS target refresh.
- **UI-003**: offline operation and two platform-specific native release builds.
- **QAL-001**: verified stress/recovery tests and explicit capture integrity report.

**P1 — Strongly recommended**

- **ANA-001**: message rate per RT/SA, per-channel bus activity, status/error statistics, periodicity/jitter estimates with provenance.
- **ANA-002**: derived anomalies such as missing expected updates, out-of-range decoded fields and unexpected RT patterns.
- **FLT-001**: saved filter presets; multi-condition AND/OR display filtering.
- **ICD-004**: mapping editor with preview, JSON schema version and revision compare.
- **PLOT-002**: measurement cursors, synchronized crosshair, units, raw/converted toggling, bookmarks.
- **REP-001**: export time-bounded data CSV/JSON, selected raw hex and engineering signal values, summary report.
- **UX-001**: searchable command palette, keyboard shortcuts, density/columns, per-user layouts, scalable fonts, color-independent alarms.
- **DIAG-001**: diagnostics page including USB health, queue depth, dropped records, process RSS, disk throughput and SDK errors.
- **SEC-001**: signed releases and signed update manifest; offline verifiable bundles.

**P2 — Later**

- Optional IRIG-B absolute-time overlay once DDC implementation verified.
- Compare/overlay multiple recorded sessions with time alignment.
- User-defined derived math channels with sandboxed expression evaluator.
- Export industry-standard Chapter 10 format if SDK capture format and requirements justify it.
- Trigger rules and capture policies; export templates; plugin developer SDK; multi-device simultaneous capture.

## 6. MIL-STD-1553 interpretation specification

### 6.1 Raw information model

A 1553 wire word comprises sync, 16-bit content and parity. The interface SDK may give 16-bit word payload and metadata rather than physical sync/parity bits; the UI must not imply direct visibility of nonprovided electrical fields. Preserve SDK error indications separately. Store both decoded message summary and immutable raw capture.

### 6.2 Command word

- Bits 15–11: RT address (5 bits, 0–31).
- Bit 10: transmit/receive direction.
- Bits 9–5: subaddress/mode selector (5 bits).
- Bits 4–0: word count or mode code (5 bits).
- For standard data transfers, word count 0 represents 32 data words.
- RT 31 is broadcast address; SA 0/31 identify mode commands.
- Interpret the following words based on transaction type and context; do not blindly label every second word status.

### 6.3 Status word and integrity

Decode recognized flags (message error, instrumentation, service request, broadcast received, busy, subsystem flag, dynamic bus control acceptance, terminal flag, reserved bits) according to applicable protocol settings. Preserve vendor-reported validity and status raw word.

### 6.4 Message types

Support BC→RT data, RT→BC data, RT→RT transfer, broadcast, mode commands, missing response, extra/partial words, malformed records, and gap/timeout analysis subject to SDK observability.

### 6.5 Timestamp semantics

Source: 48-bit/100-ns device timer where exposed. Track counter wrap and reset boundaries; detect discontinuity when device reconnects. Maintain separate capture host wall time and monotonic time. Never claim 100 ns *absolute clock accuracy*: it is a published timestamp resolution. IRIG-B synchronization is separately configured and validated.

## 7. ICD and engineering-value conversion

### 7.1 Minimum schema

`profile_id, revision, description, match:{channel,bus,rt,sa,direction,message_type}, signal_id, signal_name, word_index, bit_offset, bit_width, bit_order, numeric_type, word_order, signedness, scale, offset, unit, min_valid, max_valid, enum_values, invalid_patterns, freshness_timeout_ms, display_precision`.

### 7.2 Example — illustrative not operational

```json
{
  "profile_id":"lab-demo", "revision":"1.0",
  "signals":[{
    "signal_id":"TEMP_DEMO", "name":"Sample Temperature",
    "match":{"rt":3,"sa":2,"direction":"RT_TO_BC"},
    "word_index":0,"bit_offset":0,"bit_width":16,
    "numeric_type":"uint16","scale":0.01,"offset":0,
    "unit":"degC","min_valid":-40,"max_valid":125,
    "display_precision":2
  }]
}
```

Word content `0x03E8` -> unsigned integer 1000 -> 10.00 degC in the illustrative profile above. Actual user system semantics must come from an authorized ICD; do not use this example as a real payload definition.

### 7.3 Decoder algorithm

1. Match raw message pattern (channel, bus, RT, subaddress, direction, message type).
2. Select data word(s) and apply explicit bit indexing/word ordering.
3. Check required length, SDK integrity and profile invalid patterns.
4. Interpret unsigned/signed/BCD/enum/fixed point.
5. Compute `engineering = decoded_integer * scale + offset` only for defined linear encodings.
6. Apply valid ranges and freshness rules; return structured `{raw, value, unit, quality, mapping_revision, timestamp}`.
7. Cache converted signals using immutable mapping revision identifiers; re-decode deterministically on demand.

### 7.4 Quality states

`VALID`, `UNMAPPED`, `INVALID_LENGTH`, `INVALID_ENCODING`, `BAD_FRAME`, `OUT_OF_RANGE`, `STALE`, `AMBIGUOUS_MATCH`, `TIME_DISCONTINUITY`.

All signal screens visibly differentiate values from validity and age. Units must not be silently inferred.

## 8. Technical architecture

```text
USB MIL-STD-1553 (BU-67113Ux)
    | Vendor USB driver / AceXtreme C SDK [platform-dependent]
    v
DDCAdapter (C ABI wrapper, monitor-only state machine)
    |
    v
Acquisition workers (one per independent SDK stream if permitted)
    |-- timestamp normalization / packet metadata
    |-- bounded raw event queue -> Chunked recorder -> compressed files + checksums
    |                                   |-> SQLite sparse index + session metadata
    |-- protocol decoder -> ICD engine -> signal cache -> plot downsampler
    |-- counters/errors -> alerts/statistics
    v
Native UI dispatch (fixed cadence) -> Qt 6 Widgets views/dialogs + Qwt plots

Separate USB security key -> LicenseService -> app capability gates

Independent development seams:
  Vinay: DeviceAdapter + LicenseAdapter + Qt Widgets/Qwt UI + executable
  Ravali: Qt-free aerotrace_core library + core_cli test/verification executable
  Both: use read-only versioned contracts/ (signed off in Week 1)
```

### 8.1 Worker separation

- **Main thread**: event loop, UI state/rendering only. No heavy DB queries or USB SDK acquisition on this thread.
- **Capture threads**: minimal allocations, batched reads, stable latency; no UI widgets accessed.
- **Recorder thread**: high priority relative to noncritical analysis; sequential writes and checkpointing; bound disk backlog.
- **Decoder workers**: bounded job queue; never block recorder; mapping snapshots immutable.
- **Query/export workers**: cancellable and rate-limited to protect capture and UI.
- **License worker**: startup and periodic entitlement validation with OS hot-plug events where available.

### 8.2 Buffering policies

- Fixed-size preallocated ring(s), not unbounded `std::vector` growth.
- Backpressure and alarm thresholds at 50%, 80%, and 95%; full queue explicitly increments loss counter.
- Capture integrity indicator distinguishes `NO_KNOWN_LOSS`, `LOSS_DETECTED`, and `INTEGRITY_UNVERIFIED`.
- Keep display data as decimated bounded history; keep raw capture separate.
- Avoid `std::string` hex formatting in hot acquisition path; format only visible viewport on demand.

### 8.3 Recorder file structure (suggested)

- `session.atm` JSON/CBOR manifest: format version, app/SDK/device versions, configuration, session UUID, start/end clocks, loss summary, ICD hash and checksums.
- `chunk_000001.atb`: magic/version, source IDs, time range, raw typed packets, CRC32C, optional Zstandard compression.
- `session.db`: SQLite sparse timestamp/record offsets, bookmarks, events, derived summaries (not authoritative raw record storage).
- Keep multiple chunks, rotate by size/time, and write to a temporary name until checksummed/finalized.
- Crash scan verifies headers/CRC, rebuilds missing index, marks incomplete tail and reports exact recovered range.
- Note: index and metadata DB may be backed up, but never force every bus frame through a SQLite transaction.

### 8.4 Replay

Replay exposes the same source interface as live acquisition (`IRecordSource`). Deterministically emit raw records with original timestamps; use user-controlled clock scale. Filtering should not re-order raw sequences. Separate pause display from pause playback. Replay must work without DDC hardware but **still requires license dongle**.

## 9. Approved technology stack and dependency boundaries (v2.0)

| Component | Approved selection | Main owner | Rationale / restriction |
|---|---|---|---|
| Language | C++20 | Both | Native performance; vendor C API interoperability |
| Desktop GUI | Qt 6 Widgets / Qt Designer | Vinay | `QTableView`/`QAbstractTableModel`, forms, dialogs, accessible native desktop UX |
| GUI forms and composition | Designer `.ui` XML compiled by CMake AUTOUIC | Vinay | Editable layouts; C++ owns behavior and dynamic model/plot content; never edit generated `ui_*.h` |
| Scientific plots | Qwt 6.3.x, pinned after compatibility verification | Vinay | Technical 2D plotting; bounded live redraw, specialized cursor overlays |
| Hardware | DDC AceXtreme C SDK, validated device/driver release | Vinay | Native monitor-mode SDK wrapper; do not implement BC/RT transmission |
| Security key | Validated commercial cryptographic USB dongle SDK | Vinay | Startup and hot-removal capability gates; separate USB device |
| C++ processing | Qt-free static library `aerotrace_core` | Ravali | MIL-STD-1553 parser, ICD mapping, replay, events, exports, statistics |
| Capture storage | Versioned append-only binary chunks + CRC32C + optional Zstandard | Ravali | Immutable raw data and crash recovery; not one SQLite transaction per record |
| Index / metadata | SQLite with sparse indexed reads and migrations | Ravali | Sessions, chunks, bookmarks, metadata, search |
| ICD/config | nlohmann/json + JSON Schema validation | Ravali (ICD); Vinay (UI preferences) | Versioned and validated mapping; UI does not implement decoding |
| Queue / threading | Bounded preallocated C++ queues, worker isolation | Vinay (SDK entrance), Ravali (recorder and decode) | UI cannot block acquisition; loss evidence preserved |
| Crypto | Audited provider/vendor crypto, no homemade primitives | Vinay | Verify key challenge/response and signed bundles |
| Build | CMake + Ninja; MSVC Windows, GCC/Clang qualified RHEL | Both, module-local; integrated root only in integration stage | Avoid editing the same `CMakeLists.txt` |
| Tests | GoogleTest/CTest, synthetic fixtures, sanitizer builds | Both | Independent module gate and signed-off end-to-end acceptance |
| Operational logs | Bounded asynchronous logging, no payload/secrets | Both | Low overhead and safe diagnostics |
| SCM | GitHub private repository, code owners and separate directories/branches | Both | Avoid concurrent edits to shared source files |

**Qt and Qwt licensing gate:** Check the exact chosen Qt 6 version/module under its applicable LGPL/GPL/commercial terms, distribution/linking method, and the Qwt License Version 1.0. A protected proprietary product or static linking requires a documented compliance decision. Do **not** assume the security dongle changes open-source license obligations. Qwt may need to be built separately on Windows. RHEL Qt library, C++ runtime, driver, device and secure-key compatibility require independent qualification.

**Pinned version policy:** Start with a compatible supported Qt 6.x Widgets toolchain and Qwt 6.3.x, record exact patch/build IDs in `docs/compatibility-matrix.md`, and freeze them once qualified. Do not force a newer Qt version if the customer's RHEL release cannot support it. No Qt components are a transitive requirement of `aerotrace_core`.

**Independence rule:** Ravali must be able to build and test `aerotrace_core` with **no Qt, Qwt, DDC, USB hardware or key**. Vinay must be able to build and run `aerotrace_gui_demo` using a **mock core** with **no Ravali implementation**. Actual hardware capabilities and performance remain unverified until tested on qualified equipment.

### 9.1 GUI/backend separation and build decision (2026-10-28)

- Retain Qt Widgets and CMake. A qmake `.pro` file is not needed for Designer editing; CMake lists `.ui` files and compiles them with AUTOUIC. Do not maintain duplicate CMake/qmake application builds.
- `apps/desktop/main.cpp` is the entry point. Window coordination, page groups, table models, plot widgets and GUI checks live in separate files. `forms/` owns converted visual layouts; controllers connect actions and populate runtime data.
- `modules/core`, `modules/acquisition` and `modules/application` are Qt-free libraries. The acquisition worker owns the installed DDC MTI receive-only API, packet preservation, recording and engineering decode; the application library owns profile/replay/analysis/export services. Hardware qualification remains separate from compilation and software tests.
- All 18 screen bodies and shared layouts now use Designer forms. The PDF/OCR/evidence dialog is also Designer-editable. Controllers bind models/actions and runtime plot data; the desktop target is `aerotrace_desktop`.
- Desktop builds use `out/build/desktop-release`; independent backend builds use `out/build/core-release`. Packages use `dist/AeroTrace1553`. Never distribute build caches or another developer's Qt Creator user settings as project configuration.

### 9.2 OCR, PDF retrieval and learning extensions

These requirements now drive the ICD automation pipeline. Digital extraction, local OCR, semantic/full-text retrieval and validated profile activation are implemented; representative-document and physical-hardware acceptance remain required:

- DOC-001: Extract embedded PDF text first; render/OCR scanned pages when required. Preserve document hash, page number and extraction provenance.
- DOC-002: Provide local FTS5 BM25 and MiniLM semantic retrieval. Default answers are extractive and cite source pages; optional generation uses a configured loopback model endpoint. Model answers never directly activate mappings. Evaluate retrieval on representative ICDs before release.
- AI-001: Run heavy OCR, embedding and model inference in an optional worker process with cancellation, bounded concurrency and explicit CPU/RAM budgets. The worker must never own capture state or block acquisition/recording.
- AI-002: No cloud dependency is introduced by default. Whether local answer generation must coexist with continuous capture on the minimum PC remains an open product decision.
- ALG-001: Small deterministic signal algorithms belong in the tested C++ core. Record algorithm/model versions and configurations with derived results.
- LEARN-001: Define learning as separate capabilities: preferences, user feedback, statistical baselines, or retraining. No automatic modification of hardware modes or capture integrity policy. Models require evaluation, versioning, rollback and visible provenance.
- PERF-AI-001: Keep monitoring-only performance targets separate from OCR/model-loaded modes. Qualify capture loss, UI latency and resource use for each allowed concurrent workload; the original idle RSS target does not establish a loaded LLM budget.

## 10. Dongle-only offline licensing design

### 10.1 Security requirements

- A physically separate security key with protected private secret, not a plain USB flash drive.
- Provision per-key unique entitlement and signed certificate/feature grants; keep private secrets inside secure element.
- At startup issue unpredictable nonce, request device signature/cryptographic response, verify with pinned vendor/product public trust root.
- Include product ID, license version, feature bits, optional expiry and key identity in authenticated messages.
- Verify at startup, before each privileged operation, after OS hotplug, and periodically (recommended default 2 seconds; benchmark key polling cost).
- On missing key: stop capture and revoke active capability access immediately in application state, perform bounded safe flush, lock UI; never allow unlocked replay/offline data browsing.
- No cloud calls. Offline provisioning must use signed files or secure key programming at authorized station.
- Log state transitions without secrets, secure-element identifiers can be hashed/truncated in diagnostic views.
- Use signed binary updates and protect license-validation code with reasonable tamper-resistance; don't promise perfect DRM.
- License service exposes simple `Locked`, `Valid`, `GracefulStopping`, `Error` states; all sensitive modules gate at the service boundary.

### 10.2 Licensing vendors / economics

Commercial Thales Sentinel HL type keys have proven Windows/Linux licensing infrastructure but have nonzero acquisition and vendor SDK costs. For a lower-cost hardware prototype, evaluate a microcontroller/secure-element HID key using a trusted cryptographic implementation and hidapi, but budget engineering time for manufacturing, tamper resistance, provisioning, RHEL udev and tests. A device's USB VID/PID or serial number alone is trivially insufficient protection. Hardware choice must be finalized by procurement and a working offline RHEL proof of concept.

### 10.3 Security test cases

- No dongle, wrong dongle, unprovisioned dongle, expired feature, tampered certificate.
- Rapid unplug/replug while capturing; USB bus suspend/resume; swapping keys between machines.
- Clock changes (if time-based license), restart with no Internet, simultaneous copies on separate hosts.
- Graceful raw capture stop and recovery integrity; no stale entitlement cached across restart.

## 11. Performance specification and measurement

**All figures below are engineering targets to validate, not measured BU-67113U2 benchmarks.**

| KPI | v1 target | Test method |
|---|---|---|
| UI warm idle memory (RSS) | <= 120 MB preferred | Windows Task Manager / `smem`, 10 min |
| Live capture 1 h RSS | <= 250 MB preferred | 8 signals plotted, filters and raw capture active |
| RSS growth 8 h | <= 30 MB after warm-up | repeated live/replay workload; leak check |
| UI command response | <= 100 ms p95 | instrument input-to-frame under moderate capture |
| Live raw table latency | <= 250 ms p95 | injected tagged record to visible table |
| Plot refresh | 20 FPS default, 10–30 configurable | frame-time probe |
| Plot points onscreen | <= 3,000 per trace/viewport initially | min/max bucket or LTTB decimation |
| Signals | 8 visible traces (v1) | fixed workload benchmark |
| Recorder integrity | zero *software-attributable* loss at qualified load | compare source/record counts, hash replay |
| Recorder throughput | qualify at >= 10 MB/s continuous synthetic input | 8-hour deterministic stress |
| Peak events | qualify 25k events/s sustained, 50k burst in simulator | artificial workload, revise to real bus envelope |
| Recovery | preserve all CRC-valid completed chunks; flag tail losses | kill process and power interruption simulation |
| Cold launch | <= 3 seconds preferred excluding slow driver enumeration | repeated trials on reference host |

**Reference hardware:** baseline x86_64 4-core/8 GB RAM/SSD, plus a slower 2-core/8 GB qualification station. Prefer SSD and keep dedicated free space. OS page cache, graphics driver, GTK libraries, security-key runtime, DDC driver and SDK may affect measured RAM beyond application heap.

### 11.1 Optimization rules

1. Preallocate capture nodes and reuse buffers.
2. Fetch batches from SDK; avoid per-event dialogs/logging.
3. Write binary raw data before converting to hex strings.
4. Decouple device rate from 10–30 Hz UI cadence.
5. Virtualize table rows; render only current viewport.
6. Plot min/max bucket summaries for spikes; keep original samples on disk.
7. Index sparse timestamp intervals; do not hydrate full recording into RAM.
8. Configure WAL/checkpoint rules for SQLite session database.
9. Make long search, replay seeking and exports cancellable.
10. Benchmark separate CPU, resident memory, disk queue, USB errors, and plot frame times.
11. Quality switches: reduced motion, chart refresh, number of traces, history, advanced error correlation, compression level.
12. Never use artificial frame discards to meet memory KPI without visible data-integrity reporting.

## 12. User experience and design system

### Visual source of truth and verification

The 18 files in `UI_Screens/` are the approved visual references. Preserve navigation order, card hierarchy, spacing, colors, typography, table geometry and plot placement. Baseline comparison is at 1500×940 logical pixels and 100% scale; additionally verify short windows and 150% scale for clipping and scrolling. Reference images are immutable inputs, never replacements for functional widgets.

Require rendered evidence for every screen and a tracked list of differences. A successful render test does not establish pixel equality. Dynamic records, paths, timestamps, simulation/SDK status and unsupported-feature labels must remain accurate even when their reference text differs. Pixel identity has not been achieved by the initial restructuring and remains an open acceptance item.

**Style:** polished laboratory instrumentation; light mode; restrained professional color. **Never** neon/glass-heavy UI that reduces scientific readability.

- Background `#F5F7FB`; surfaces `#FFFFFF`; borders `#E3E8F0`; main text `#1C2738`; secondary `#69788C`.
- Brand blue `#2563EB`; teal `#0D9488`; violet `#8B5CF6`; amber `#D97706`; red `#DC2626`; green `#15803D`.
- Sidebar ~232 px; app header ~68 px; major canvas grid 12 columns; 8 px spacing scale.
- Fonts: Inter if legally distributable, otherwise Segoe UI on Windows/system sans on RHEL; monospace tabular font for hex/timestamps.
- Chart signals have distinct colors **and** line-style/labels for accessibility; do not use color alone for error status.
- Dynamic table columns resizable, sortable and persisted; use visible row count 50–100 initially.
- Use terminology consistently: Channel 1/2, Bus A/B, Monitor (MT), BC, RT, SA, T/R, raw hex, engineering units, quality.
- Recording state always visible in top bar, including elapsed duration, disk, drop state and license presence.
- Destructive actions require clear confirmation only when data loss is possible; pausing display must never pause capture silently.

### 12.1 UI global navigation

Overview | Live Monitor | Engineering Values | Plots | Message Inspector | Topology | Events & Health | Sessions | Replay | ICD Manager | Exports & Reports | Device Setup | License & Security | Preferences | Help & Diagnostics.

### 12.2 Required UI screens and acceptance specs

**S01 License Gate:** full clean product illustration; left product benefit summary, right large secure-key illustration; states Key Missing / Checking / Licensed / Error. Primary Retry & Exit; no functional navigation underneath.

**S02 Welcome / Recent:** quick connect, open recording, recent sessions list, device presence, license status, storage health, onboarding checklist, offline badge.

**S03 Device Setup:** detected DDC family/model, serial masked, firmware/driver/SDK, number of channels, enable Channel 1/2, Bus A/B toggles, timestamp source, Monitor-Only banner and transmit-inhibit verification; test connection.

**S04 Live Overview / Dashboard:** fixed header with CAPTURING + REC; total messages, rate, active RTs, errors, capture integrity, disk capacity, time trend, RT activity summary, recent events.

**S05 Live Message Monitor:** biggest working screen; dense virtualized table, command/status/hex word columns, timestamp, channel, bus, RT, SA, direction, word count, filters; split message detail pane. Raw table pause only affects viewport.

**S06 Message Inspector:** exact selected frame and DDC metadata; 16-bit command word bit diagram, status word, data words hex/binary/decimal, transaction timeline, detected integrity errors, engineering matches.

**S07 Engineering Values:** signal lookup, tiles with units, freshness/status, tabular trending, favorite/pin actions, mapping profiles and unmapped count.

**S08 Live Multi-Plot:** 2x2 plot grid, add up to 8 traces, plot legend, time window, cursor measurements A/B, live-follow, pause display, plot export, time sync.

**S09 Bus / RT Topology:** interactive 2-channel bus A/B topology diagram and RT activity nodes; select RT to filter; passive-observation label, no control transmission actions.

**S10 Events & Alerts:** error timeline, counts by type/criticality, parity/status/timeout/incomplete/error SDK records, missing-update inferred rules, event-to-frame jumps, bookmarks.

**S11 Session Library:** searchable recordings, duration, status, filesize, device/channel, ICD revision, integrity indicator; open, rename, annotate, archive, delete confirmation.

**S12 Replay Workbench:** source file, timeline scrubber with markers, step, 0.25–5x playback, raw and decoded tracks, bookmarks, synchronized plot/table, recording integrity ribbon.

**S13 ICD Library:** profile list, version/hash, import/export/clone, mapping search, match conflicts, validation status and apply-to-live.

**S14 ICD Signal Editor:** left mappings tree, center form for RT/SA/word/bit/type/unit/scaling, right raw-word preview with before/after and golden test vector; Save Draft vs Publish revision.

**S15 Export & Report Builder:** choose session/time span, raw vs decoded, filters, CSV/JSON/binary, delimiters, units, anonymization, destination, estimated size, export progress/cancel and checksum manifest.

**S16 License & Security:** physical key status, feature grants, offline mode, renewal/import signed entitlement action if vendor supports, application integrity signature, local logs (no secret disclosure).

**S17 Preferences:** theme fixed light initially; typography, accessibility, plot default refresh, history, recording folder, chunk rotation, disk thresholds, language, session defaults, privacy.

**S18 Help / Diagnostics:** build ID, OS/kernel, DDC driver/SDK, USB connection, device monitor test, queue counters, file format, key SDK, self-test/export diagnostic bundle with sensitive-data review.

### 12.3 Global dialogs / microstates

- Start Session, Stop Session, Discard Temporary Session, Disk Space Low, Queue Near Full.
- Dongle Removed During Recording, Device Detached, Driver Mismatch, Driver Permission Denied (RHEL udev), Timestamp Reset.
- Invalid ICD, Ambiguous Signal Mapping, Export Complete/Failed, Crash Recovery, Update Signature Rejected.
- Empty state (no traffic), Loading state (indexing), Error state (device absent), Recording active, Replay active.

### 12.4 Keyboard and accessibility

`Ctrl+O` Open session; `Ctrl+F` Search; `Ctrl+R` Start/stop recording only when safe context; `Space` Toggle view replay pause (not live acquisition); `F1` Help; `Esc` dismiss noncritical overlays. High-contrast focus rings, table keyboard traversal, screen-reader accessible metadata, color and textual state labels.

## 13. Screen state and navigation rules

- The app shell is mounted only after license success; on failure display S01 with no navigation.
- S02 -> S03 -> S04 for first capture. S04 switches to S05/S07/S08 without stopping acquisition.
- From any recorded frame -> S06 Inspector; from an error -> S10 and S06; from S08 plot cursor -> corresponding raw frame in S05/S06.
- S11 -> S12 replay, optionally S08/S07 synchronized with replay source; S13/S14 changes mapping revision but never mutates raw source.
- S15 reporting can run with recording in progress subject to throttling; warn about incomplete active capture range.
- On dongle removal, every screen yields to S01, after a bounded safe capture-stop state.

## 14. Data model / SQLite metadata entities

- `devices(device_id, model, firmware, sdk_version, last_seen)`
- `sessions(session_id, label, start_host_time, end_host_time, status, channel_mask, path, raw_format_version, icd_revision_hash, integrity)`
- `chunks(chunk_id, session_id, seq, path, start_device_time, end_device_time, record_count, bytes, crc)`
- `record_index(session_id, channel, timestamp, chunk_seq, byte_offset, record_seq)` **sparse index only**
- `signals(signal_id, profile_revision, name, unit, word_mapping, quality_policy)`
- `events(event_id, session_id, timestamp, record_seq, category, severity, source, annotation)`
- `bookmarks(bookmark_id, session_id, timestamp, record_seq, title, notes)`
- `exports(export_id, session_id, range, format, file_hash, finished_at, status)`
- `profiles(profile_id, revision, hash, schema_version, created_at, provenance)`
- `preferences(local_user_profile, key, typed_value)`

Use parameterized queries and migrations. Store sensitive diagnostic IDs carefully; avoid raw trace contents in operational logs.

## 15. Packaging and installation

### Windows

- Release target: signed `AeroTrace1553.exe` (x64), with the approved Qt 6 Widgets and Qwt runtimes deployed according to license and technical constraints; app-owned C++ code may be statically linked where appropriate.
- DDC USB driver and any vendor DLL remain separately installed or packaged according to vendor license and driver signing rules.
- Sentinel-type key may require an additional runtime component, or a driverless configuration if the selected key supports it.
- Provide signed installer/portable bundle; installer probes prerequisites, never modifies driver silently.

### RHEL

- Native x86_64 ELF with approved Qt 6 Widgets/Qwt libraries built for a compatibility-appropriate RHEL toolchain; RPM/portable offline bundle recommended.
- System's glibc/GTK/udev remain external dependencies; DDC USB module or library installed in approved way.
- Secure dongle may need udev rule or vendor runtime. Validate no elevated privileges are required during ordinary operation.
- Test SELinux enforcing, Secure Boot module signing when applicable, nonroot operator login, X11/Wayland session specifics and system policy.

### Delivery checklist

For each platform release publish: executable, vendor driver compatibility guide, SHA-256 checksums, digital signature, legal notices, license key requirements, known limitations, test report, rescue/recovery instructions, and optional diagnostic tool. No remote licensing dependency.

## 16. Testing and acceptance plan

### Independent-module verification gates

- **Vinay/UI+hardware:** CI builds with mock core, launches Qt UI in a permitted display/offscreen test session, validates all 18 screens, view-state transitions, nonblocking UI, chart bounds and role-specific lock states. Real device and dongle tests are additional and cannot be simulated away.
- **Ravali/core:** Qt-free CI builds on supported C++ compilers, CTest runs command/status and ICD golden vectors, exact round-trip record/replay, corrupt-chunk recovery, SQLite migration and bounded synthetic throughput. No vendor SDK or physical dongle required.
- **Contract:** Both independently compile against the frozen `contracts/v1` headers and fixture JSON/binary samples. The interface/fixture compatibility job runs on each PR; violating schema/ABI requirements blocks integration.
- **Evidence:** Every completed work package includes build command, test command, exit code, automated test results, a short demonstration artifact/screenshot for GUI tasks or test output for core tasks, and a limitations/known-failures note.

### Unit tests

- All 1553 command/mode/status bit combinations required by the supported standard.
- WC=0->32, RT31 broadcast, SA0/31 mode commands, signed/unsigned decode, negative two's complement, word swapping, bit masking, BCD validity, 16/32-bit field boundaries, NaN/overflow policy.
- Timestamp wrap, monotonicity across USB reconnect, IRIG-B correlation policy.
- License state transitions; no authorization through stale/forged USB identification.

### Integration tests

- Official DDC monitor SDK samples first, before application integration.
- Verify exact BU-67113U2 discovery, channels, bus A/B, non-transmitting monitor safety, driver error messages.
- Compare known bench-generated reference traffic to host normalized capture.
- Concurrent capture+plot+SQLite query+export without source stalling.

### Endurance / negative tests

- 8 h nominal plus 24 h soak; plot widgets opened/closed repeatedly; stress with display minimized and maximized.
- USB DDC disconnect, license dongle removal, USB controller reset, sleep/resume, filesystem permission failure, low disk, sudden kill, corrupt chunk and partial index.
- Capture loss accounting: known generated count versus raw committed count, capture/SDK overflow counters, crash report.
- Benchmarks on Windows 10/11 and each target RHEL release/kernel; report CPU/RSS/p95 UI latency, write throughput and p99 queue occupancy.

### Release acceptance / Definition of Done

- P0 requirements verified on both systems with exact device and authorized driver releases.
- All 18 screens specified and tested across no-data, connected, capturing, replay, error and license-locked states.
- Raw capture after normal stop is byte-for-byte replayable; interrupted sessions recover validated chunks.
- ICD vectors yield verified engineering values, quality flags, and mapping revision provenance.
- No active BC/RT transmission in monitor product; test instrument verifies transmit inhibit/safety behavior.
- Hardware key required for all useful application paths including offline replay; removal stops operations and locks.
- Zero software-attributable loss at proven rated test profile; truthful overflow evidence for overload scenarios.
- Performance metrics met in repeated measurements, including <=250 MB preferred RSS during qualified workload or documented exceptions.

## 17. Parallel two-developer implementation roadmap (replaces sequential v1 plan)

**Planning assumption:** two developers with overlapping availability, a GitHub repository, a qualified Windows development PC (Vinay) and browser/cloud/CI access for Ravali. Original sequential 20-week target is retained as an *indicative planning envelope*, not a delivery guarantee; all hardware/vendor gates can move dates. Work packages below total **100 relative effort points for Vinay and 100 for Ravali**; these are estimates, not verified developer hours. Rebalance if measured effort deviates by more than approximately 15%.

| Period | Vinay — independent UI/hardware workstream | Ravali — independent Qt-free core workstream | Joint verification checkpoint |
|---|---|---|---|
| Week 1 | Validate installed Qt/C++ toolchain; open Qt demo, create UI module skeleton | Choose browser-only/remote C++ environment; compile minimal CLI | Freeze `contracts/v1` (data types, source and control interfaces, fixture format); agree acceptance proof |
| Weeks 2–3 | Qt Widgets shell, navigation, UI components, state machine and mock source | C++ event model, 1553 command/status parser, fixture loader, CLI | Contract compile checks pass independently; no cross-folder edits |
| Weeks 4–5 | Device/setup screens, dense `QTableView`, inspector | ICD parser/validation and golden engineering decode tests | Same golden fixtures yield matching visible and CLI interpretations |
| Weeks 6–7 | Dashboard, engineering tiles, Qwt plots and decimation | Recorder queue, binary chunks, CRC32C, recovery tests | GUI smooth on synthetic stream; core round-trip checksum pass |
| Weeks 8–9 | DDC SDK adapter and monitor-only bench in qualified Windows environment; mock if blocked | SQLite sparse index, metadata migrations, replay source | Both demos reproducible; real device safety evidence if available |
| Weeks 10–11 | License adapter, lock/reconnect UI, vendor key tests; RHEL driver feasibility | Analytics, error events, alerts, bookmarks and replay seek | Contract event/status states verified using fixtures |
| Weeks 12–13 | Complete all 18 screen behaviors, Qwt cursors, UX/accessibility and keyboard shortcuts | CSV/JSON/binary exports, session search, CLI workflows | No dependency on other developer implementation to complete module test |
| Weeks 14–15 | UI screen regression, CPU/RAM tests, Windows packaging and RHEL preparation | Stress/recovery/performance validation and packaging the `aerotrace_core` artifact | Each module passes independent Definition of Done; sign-off for integration |
| Weeks 16–17 | Integrate UI + adapter into shared `integration/v2` branch, own linker/runtime assembly | Integrate core, reconcile data mapping, fix core contract mismatches | End-to-end synthetic capture→record→replay→plot→export gate |
| Weeks 18–20 | Qualified OS/device/dongle tests, UI/installer defects | Full core integrity and soak support; data/export correctness | Paired final acceptance, release readiness, signed offline artifacts |

**Practical rule:** At each weekly checkpoint exchange test reports and versioned mock/fixture artifacts; do not merge unverified, still-changing code into the integration branch. Independent branch work is viable because of frozen data/control contracts, not because branch separation by itself eliminates integration failures.

**Hard dependencies:** Actual DDC monitor mode, transmit-inhibit safety, key provisioning, driver compatibility and cross-platform release cannot be claimed complete from simulators, remote cloud builds or screenshots. If vendor hardware is unavailable, continue independent software work but mark HW-01..HW-06 and LIC hardware gates **BLOCKED**, not **PASS**.

## 18. Risks, mitigation, ownership

| Risk | Impact | Mitigation / owner |
|---|---|---|
| Exact card/SDK/RHEL mismatch | Blocks hardware path | M0 hardware qualification; DDC support; vendor compatibility matrix |
| Proprietary SDK cannot link statically | Single-file goal unmet | App-owned code static, approved vendor redistributables/installer |
| Secure dongle runtime needs service/udev | Extra installation | Validate key SDK on air-gapped RHEL before purchase |
| Busy UI causes capture loss | Data invalidity | recorder-priority pipeline + bounded UI + loss metrics |
| Host disk stalls/fills | Missing capture | reserve threshold, queue telemetry, loss alarms and safe stop |
| Incorrect ICD scales/bit ordering | Wrong engineering values | golden vectors, revision/hash and explicit quality states |
| Missing/ambiguous timestamp semantics | Invalid timing conclusions | preserve all clock domains, tag discontinuity, cite source |
| Vendor SDK provides limited malformed word detail | Incomplete error reporting | capability detect, gray out unsupported error metrics |
| Proprietary Qt/Qwt redistribution obligations | Legal/blocking | evaluate LGPL/commercial Qt, Qwt License 1.0, redistribution and static/dynamic linking before release |
| Single-binary Linux portability impossible | Installation complexity | tested offline RPM/portable bundle, compatibility baseline |
| Key removal during capture | Interrupted evidence | Vinay key gate + Ravali core safe finalize + explicit interruption metadata |
| Frozen interface diverges between branches | Late integration failures | joint contract review, immutable `contracts/v1`, versioned fixtures, CI compatibility tests |
| Same file edited by both members | Git conflict/review confusion | explicit path ownership, PR-only shared changes, no concurrent root CMake edits |
| Ravali has little local disk space | Blocks core development | no local Qt install; browser `github.dev` for editing and remote Codespaces + CI builds for core |
| Hosted build/cloud service inaccessible or quota exceeded | Slows Ravali's verification | budget review, alternative remote VM/borrowed machine/external SSD; never treat editor-only work as compiled verification |

## 19. Open decisions and exact info to collect

1. Target **RHEL 8, 9, or 10**, kernel string (`uname -r`) and architecture (`uname -m`).
2. Exact device part/revision, actual channel count, driver + SDK versions; ideally the DDC hardware information sample output.
3. Vendor-approved API, advanced monitor configuration, timestamp and error-record structures.
4. Which signals and payload mapping ICD are in scope? Provide a sanitized test ICD, not confidential operational data.
5. Expected maximum capture duration, local retention and output throughput; SSD vs HDD.
6. Whether app must truly be only one user-visible executable, or an offline installed package with vendor dependencies is acceptable.
7. Selection and budget for Sentinel-type dongle versus a custom secure-element HID prototype.
8. Whether charts require generic engineering values only or scientific features such as FFT, histograms and spectral analysis later.
9. Whether session files need encryption at rest, mandatory redaction, audit approval, secure deletion and classified-network handling policies.
10. Confirm Ravali's local free disk, RAM, available Internet/cloud compute, and access to a private GitHub repository; decide Codespaces vs local lightweight toolchain.
11. Confirm Vinay's exact Qt version, compiler kit, Qwt 6.3.x installation, hardware/SDK access and Windows/RHEL qualification machines.
12. Confirm GitHub handles and organization/repository ownership before filling CODEOWNERS; do not invent identities.
13. Confirm who buys/provisions physical license keys and who authorizes and supervises hardware-only safety tests.

## 20. Reference sources

- DDC BU-67113Ux family: https://www.ddc-web.com/en/connectivity/databus/milstd1553-1/boardsandboxes-1/bu-67113ux?partNumber=BU-67113
- DDC AceXtreme SDK: https://www.ddc-web.com/en/databus/milstd1553-1/software-1/bu-69092
- DDC sample code including Qt examples: https://www.rtipsonline.com/WebPages/downloads-ddcresources-sample-code.html
- Qt Widgets / CMake: https://doc.qt.io/qt-6/cmake-get-started.html
- Qt Widgets model/view: https://doc.qt.io/qt-6/model-view-programming.html
- Qwt technical plots, Qt 6 compatibility and Qwt License v1.0: https://qwt.sourceforge.io/
- Qt licenses and module exceptions: https://doc.qt.io/qt-6/licensing.html
- GitHub code owners: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners
- GitHub protected branches: https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches
- Ravali lightweight browser editor: https://docs.github.com/en/codespaces/the-githubdev-web-based-editor
- Codespaces cost/storage: https://docs.github.com/en/billing/concepts/product-billing/github-codespaces
- Qt static/deployment and LGPL obligations: https://doc.qt.io/qt-6/deployment.html and https://www.qt.io/development/open-source-lgpl-obligations
- Sentinel HL key descriptions: https://docs.sentinel.thalesgroup.com/softwareandservices/ldk/LDKdocs/SPNL/LDK_SLnP_Guide/GettingStarted/EndUser_Keys.htm
- Sentinel runtime requirements: https://docs.sentinel.thalesgroup.com/softwareandservices/ldk/LDKdocs/SPNL/LDK_SLnP_Guide/Distributing/Distributing_LDK/Software_EndUsers.htm

---

## 21. Two independent modules, ownership and equal-effort allocation

### 21.1 Operating model (approved proposal)

The product has **one native desktop executable at release**, built from **two independently buildable source deliverables**:

- **Module V — Vinay / Desktop + Device & Security (`aerotrace_gui_demo`)**: Qt 6 Widgets, Qwt, all 18 application screens, screen state and interaction, live table/plots, DDC monitor-only adapter, license-key adapter, mock-core implementation for development, app-level UI tests and release/deployment tooling.
- **Module R — Ravali / Offline Data & Analysis Core (`aerotrace_core` + `aerotrace_core_cli`)**: Qt-free C++20 MIL-STD-1553 parser, timestamp integrity handling, ICD validation/decoding, recorder, compression, SQLite indexing, search, session replay, statistics/alerts, exports, synthetic source, CLI and core tests.

**Equal division is defined as equivalent planned engineering effort, not equal screen counts or number of files.** Module V carries Qt/SDK integration complexity; Module R carries algorithm, persistence, data-integrity and stress-testing complexity. Both are substantial. Technical risk is not strictly equal; reassess estimates after Weeks 3 and 8.

**Owner constraints:** Vinay already has Qt Creator/C++ available, so Qt, Qwt, hardware SDK and GUI integration remain on his machine. Ravali's system may be low on storage; her module deliberately does not require Qt Creator, Qt, Qwt or the proprietary DDC SDK to build. A compiler+build runner **is still mandatory** to verify C++ correctness (remote is acceptable).

### 21.2 Vinay: scoped work packages and verification

| ID | Points | Tasks / source ownership | Verification / acceptance evidence |
|---|---:|---|---|
| V01 | 9 | Qt CMake project under `apps/desktop/`; UI bootstrap; C++20, Qt Widgets and Qwt version pin; fixed typography/colors; loading/empty/error states | `cmake -S apps/desktop -B ...` and Qt demo build succeed; startup works offline; environment report recorded |
| V02 | 16 | Convert S01–S18 from HTML *concept* into real Qt Widgets screens; shared sidebar/topbar/widgets, navigation, dialogs and accessible controls | 18-screen manual/automated navigation checklist; correct locked/loading/capture/replay states; screenshot evidence and zero unreachable screens |
| V03 | 12 | `QTableView` backed by bounded `QAbstractTableModel`; S05/S06 dense inspector, per-bus/channel filters, S09 topology, S10 event navigation | Scroll and filter 1,000,000 indexed synthetic records via bounded viewport without loading all rows; no UI-thread synchronous scan |
| V04 | 13 | Qwt multi-panel plots S07/S08/S12; eight traces, redraw limiter, decimation, cursor A/B, zoom/pan, linked replay timeline | Eight traces within 10–30 FPS configured range; original recorded samples unchanged; comparison screenshots and input-latency samples |
| V05 | 22 | DDC `DeviceAdapter` wrapper in `adapters/ddc/`; enumerate model/firmware, monitor-only channel/bus setup, SDK batching, hardware/host timestamps and error flags, reconnect and Tx inhibit | SDK sample works first; on authorized bench prove no BC/RT transmit; channel A/B identity recorded; driver/SDK errors reproducible; Windows then target RHEL matrix, blocked if hardware absent |
| V06 | 10 | `LicenseAdapter` and Qt lock/unlock experience in `adapters/license/`; secure challenge-response via vendor library, periodic/hotplug checks, safe stop request on removal | Wrong/missing key locks; key removal triggers stop/flush through core port; replug never auto-arms; real key evidence mandatory |
| V07 | 7 | UI dispatch/event batching; mock-core adapter; thread ownership; cancelable long-running UI operations; error/recovery screen states | UI never calls SDK or record search on GUI thread; 100 ms p95 action goal on reference workload or documented exceptions; mock-only build works without Ravali core |
| V08 | 5 | Windows/RHEL app deployment wrapper, offline packaging and runtime dependency matrix; installer checks; qualified signed binaries | Run installer/uninstaller/relaunch tests on supported machine; no Internet call; license/driver prerequisites stated, no silent driver install |
| V09 | 6 | GUI tests, screen-level regression, UI CPU/RSS profiling, accessibility/keyboard verification, independent proof package | Screenshot+test report for all 18 screens; crash/reconnect UI test matrix; artifacts in `evidence/vinay/` |
| **TOTAL** | **100** | **Vinay's independent deliverable** | **GUI demo and approved hardware adapters pass module gates independently** |

**Vinay must NOT own or edit:** `modules/core/**`, `tools/core_cli/**`, `tests/core/**`, core binary schema implementation, SQLite migrations, ICD algorithms. Integration fixes to Ravali-owned code use an issue/PR reviewed by Ravali.

**Vinay's mock GUI source:** implement a deterministic `MockCorePort` behind `contracts/v1`, including playback controls, signal quality, session lists, queue status, simulated license states, errors, and example ICD engineering values. This is test scaffolding and must be clearly marked `SIMULATED`; it must not claim hardware capture or successful physical dongle validation.

### 21.3 Ravali: scoped work packages and verification

| ID | Points | Tasks / source ownership | Verification / acceptance evidence |
|---|---:|---|---|
| R01 | 17 | Qt-free `modules/core/protocol/`: command/status word parsing, BC↔RT, RT↔RT, broadcast/mode codes, raw record normalization, timestamps with provenance and loss markers | Golden tests for WC=0→32, RT31, SA0/31, malformed frames, timestamp wrap/reset, provenance; 100% required vectors pass |
| R02 | 17 | Versioned ICD schemas/JSON validation; numeric signed/unsigned/BCD/enum/multiword decoding, scale/offset, bit order, quality flags, cache by mapping revision | Illustrative 0x03E8→10.00°C vector plus negative/overflow/invalid/ambiguous-mapping cases; deterministic replay equality |
| R03 | 18 | Preallocated bounded recording queues; append-only binary chunk writer, checksums, optional Zstd, flush/rotation, disk thresholds and crash recovery | Exact raw round-trip after stop; corrupt/truncated chunk detected; backpressure/loss counters explicit; no unsupported zero-loss guarantee |
| R04 | 9 | SQLite sparse indices, schema migrations, sessions/chunks/events/bookmarks metadata, async-query APIs | Migration from empty and prior schema; indexed seek on generated 1M-record data; RAM remains bounded; concurrent recording/index query test |
| R05 | 11 | `IRecordSource` offline replay, timestamp/frame seek, step and 0.25×/1×/2×/5× playback, deterministic event order | 100% record order/data reproduction against fixture; seek and replay tests pass; no hardware or Qt required |
| R06 | 9 | Message rates, RT/SA activity, anomaly/error evidence, statuses and freshness, quality-aware event summaries | Golden rates/alerts, no false physical-error claims; alert links trace back to record ID and time; tests on bad/partial metadata |
| R07 | 6 | Core export services for CSV/JSON/binary, time filters, unit labels, hashes and cancelation (UI produces final report layout) | Export/import validations, exact time bounds, checksums, partial-run cancellation, reasonable resource bounds |
| R08 | 7 | `tools/core_cli/` independently runnable CLI; deterministic synthetic fixture generator and JSON scenario runner; CMake/CTest CI | `core_cli generate`, `verify`, `decode`, `replay`, `benchmark` run remotely; no Qt/SDK/dongle dependencies |
| R09 | 6 | Core sanitizer/stress/recovery matrix, diagnostics and performance report, module-level documentation | 25k/s sustained, 50k/s bursts, ≥10MB/s synthetic writer qualification targets benchmarked; 8-hour test where resources permit; failures reported with evidence |
| **TOTAL** | **100** | **Ravali's independent deliverable** | **Versioned static library + CLI + test vectors and performance evidence** |

**Ravali must NOT own or edit:** `apps/desktop/**`, `adapters/ddc/**`, `adapters/license/**`, `tests/gui/**`, Qt and Qwt binary/config, platform installers. Any new UI/API requirement is filed as a versioned contract change request.

**Core ownership boundary:** Ravali owns record persistence and decoding, including shutdown/finalization behavior. Vinay owns the **SDK-to-contract producer** and the **license state signal**; the core never directly talks to USB. A critical dongle-removal stop must be triggered by Vinay's application controller and honored by Ravali's core recording API. The fully integrated timing/stop behavior needs both developers to verify.

### 21.4 Do-not-overlap / review and ownership rules

- Only the designated owner may directly change implementation files inside their directories; the other developer reviews or raises an issue.
- Both can read all source. Shared `contracts/`, top-level `CMakeLists.txt`, `.github/workflows/`, `.github/CODEOWNERS`, `docs/compatibility-matrix.md` and final installer integration are **joint-controlled**. Initial create/freeze in Week 1; later modifications require two-person review and a contract version change where applicable.
- Keep UI-specific CMake and core-specific CMake in their own directories; do not repeatedly edit a single root project file in parallel.
- Mock fixtures and field IDs are frozen as part of the contract; test harness implementations live within each developer's own module.
- Both developers use distinct working branches and draft PRs; avoid direct commits or force pushes to `main`. Git conflicts can still arise from shared files, so mandatory review/checks are the mitigation—not a guarantee of zero conflicts.

## 22. Source layout, contracts and integration interfaces

### 22.1 Proposed repository tree

```text
AeroTrace1553/
├── README.md                                # jointly approved
├── CMakeLists.txt                           # integration owner only; no concurrent edits
├── cmake/                                   # frozen base/toolchain helpers
├── contracts/v1/                            # SHARED, FROZEN AFTER WEEK 1
│   ├── include/aerotrace/contracts/*.hpp    # Qt-free C++20 public structs/ports
│   ├── schema/*.json                        # wire/ICD/session schema versions
│   ├── fixtures/{raw,icd,replay,events}/    # deterministic interchange vectors
│   └── CONTRACT_CHANGELOG.md
├── apps/desktop/                             # VINAY ONLY
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── widgets/                              # 18 screens, shell, tables, forms
│   ├── plotting/                             # Qwt plots/overlays
│   ├── controllers/                          # UI state dispatch/ports
│   └── mock_core/                            # contract-compatible fake backend
├── adapters/ddc/                            # VINAY ONLY; vendor SDK excluded from Git
├── adapters/license/                        # VINAY ONLY; no key secrets in repo
├── modules/core/                            # RAVALI ONLY; NO QT DEPENDENCIES
│   ├── CMakeLists.txt
│   ├── protocol/
│   ├── icd/
│   ├── recording/
│   ├── index/
│   ├── replay/
│   ├── analysis/
│   └── export/
├── tools/core_cli/                           # RAVALI ONLY
├── tests/gui/                                # VINAY ONLY
├── tests/core/                               # RAVALI ONLY
├── tests/contract/                           # JOINT: fixed/golden compatibility suite
├── packaging/{windows,rhel}/                # VINAY drafts, JOINT final review
├── evidence/{vinay,ravali,integration}/     # independent test artifacts
├── docs/{prds,compatibility-matrix.md,adr}/ # shared doc/release gate
└── .github/{workflows,CODEOWNERS}/           # shared; freeze/PR-review process
```

This is the **desired structure**, not a claim that any directories or source files already exist. The provided HTML file is a UI reference and remains `docs/reference/AeroTrace_1553_UI_Prototype.html`, labeled simulation-only.

### 22.2 Contract freeze — complete jointly in Week 1

Create a tiny, Qt-free `contracts/v1` API before implementing production code. Both developers sign a contract checklist (field semantics, lifetime, error policy, thread ownership, test fixtures). Avoid leaking vendor SDK typedefs or Qt types through public headers.

**Record and metadata model (minimum fields):**

- `RawRecord`: schema version; monotonically assigned host ingestion sequence (scope explicitly defined); device/channel ID; physical bus A/B; origin SDK record kind; original 16-bit words and **word count**; raw SDK flags and observed error fields; device timestamp raw+resolution+reset/wrap epoch; host monotonic acquisition tick and optional wall-time correlation; provenance and integrity state. Preserve unknown SDK fields in a versioned opaque metadata section when lawful/feasible.
- `EngineeringSample`: mapping/profile revision/hash, signal ID, original record sequence, device time, raw word/bit value, engineering numeric/enum value or invalid state, units, quality code.
- `Event`: event ID, source record sequence (if any), timestamp/channel/bus, category, severity, physical-vs-derived provenance, message.
- `SessionSummary`: session ID, file format version, start/end, channel coverage, size, loss state/counts, profile hash, available bookmark/range metadata.
- `DeviceStatus`: SDK/model/driver versions, discovered channels and buses, monitor-only/Tx-inhibit capability and verified-state flags, errors/reconnect lifecycle.
- `LicenseStatus`: locked/valid/stopping/error, approved features, dongle-present *verified* status; no private keys or raw cryptographic secrets.
- **Data identity:** device-provided timestamps are not presumed absolute wall time; `record_seq`, channel/bus and epoch disambiguate records through counter wraps/disconnects.

**Port surface (semantic operations, not a frozen code snippet):**

```cpp
// Contract API proposals — finalize actual C++ declarations together in Week 1.
// Only standard C++20 types may appear in contracts/v1 public headers.
struct RecordBatchView;              // immutable, documented lifetime; bounded size
struct StreamHealthSnapshot;         // counters, queue occupancy, loss provenance
struct SessionDescriptor;            // paths/ids/status; no Qt types
struct ReplayCursor;                 // timestamp + epoch/record-seq semantics

// Vinay's acquisition adapter publishes records into a bounded core sink.
// Ravali's core provides recorder, decoder, query, replay and export services.
// Vinay's GUI consumes bounded snapshots/results asynchronously.
// App controller owns license/device lifecycle and issues stop/flush requests.
```

**Critical choices to freeze, with tests:**

1. `RawRecord` encoding and lifetime ownership; no pointers into a vendor-owned SDK buffer after callback returns.
2. `record_seq` scope and monotonicity; cross-channel ordering and timestamp disambiguation.
3. Binary chunk header, endian, size limits, CRC coverage, schema versions, unknown-field behavior.
4. Queue-full handling and loss counters; source errors distinguished from software drops.
5. Thread-safety and cancelation, error codes and `std::expected` availability policy (C++20 means no C++23-only types).
6. Async notification model: **never** call Qt objects from the capture/recorder thread; Qt UI adaptor posts bounded snapshots to GUI thread.
7. Licensing event `stop_and_finalize()` contract, bounded timeout and what to label incomplete if flush fails.
8. ICD bit/word indices, unsigned/signed representation and timestamp quality; use shared golden fixtures.
9. File export interface and seek range semantics, including inclusive/exclusive end-times.
10. Contract semantic versioning and compatibility check. An incompatible change requires `contracts/v2`, shared approval and both test suites updated.

### 22.3 Independent build commands (illustrative, to implement)

**Vinay — Qt/Windows UI in mock mode:**

```powershell
cmake -S apps/desktop -B build/vinay -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAEROTRACE_USE_MOCK_CORE=ON
cmake --build build/vinay --parallel
ctest --test-dir build/vinay --output-on-failure
# Run the generated aerotrace_gui_demo executable; do not claim hardware attached.
```

**Ravali — core-only Linux/Codespaces or lightweight PC:**

```bash
cmake -S modules/core -B build/ravali -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAEROTRACE_WITH_QT=OFF
cmake --build build/ravali --parallel 2
ctest --test-dir build/ravali --output-on-failure
# core_cli is a separate target/test harness; exact path determined by CMake.
```

**Important:** These commands are **desired acceptance interfaces** for the future repository. They cannot be executed against the current HTML/PRD attachments and are not evidence that a codebase has already been implemented. Actual Qt Creator generator/kit options and installed Ninja must be validated on Vinay's PC.

## 23. Ravali low-storage development plan

**Primary route: browser editing + remote C++ compilation.** Ravali does **not** need Qt Creator, Qt libraries, Qwt, DDC drivers, USB hardware, Windows installer SDK or local test recordings on her own PC.

| Route | Local disk use | Can compile/run C++? | Recommendation |
|---|---|---|---|
| `github.dev` (VS Code in browser) | Minimal | **No** (editor only, no compute/terminal) | For reading, light editing, PR reviews; not sufficient by itself for verification |
| GitHub Codespaces (Linux cloud environment) | Minimal on local device; cloud quota consumed | **Yes** with GCC/Clang/CMake/Ninja | Preferred for development + interactive terminal tests, subject to Internet/plan limits |
| GitHub Actions CI only + browser edits | Minimal | **Yes**, on CI runners; slower debug loop | Budget-limited fallback; inspect logs/artifacts for verification |
| Remote SSH to an approved spare PC/VM | Minimal on local device | **Yes**, remote resources | Good alternative if Codespaces unavailable or organization policy prohibits cloud |
| Local light C++ installation + external SSD | Variable; not zero | **Yes** | Use only if safe disk space and toolchain can be provisioned |

**Setup tasks (Ravali's Week 1):**

1. Verify an approved private GitHub account/repository, network availability and policy on cloud usage. Use a **private repo** for proprietary code and sanitized fixtures only; never upload licensed DDC SDK installers, production ICDs, secret keys or sensitive traffic without explicit permission.
2. Open `github.dev` from the repository (press `.` on GitHub). Confirm edits and commits work; remember it has no compile terminal.
3. Start a Codespace from Ravali's branch with a slim C++ devcontainer or an approved remote Linux VM; verify `g++ --version`, `cmake --version`, `ninja --version`, `ctest --version`.
4. Build a 15-line C++20 CLI smoke test and run CTest; record stdout/compiler identity as her first verification artifact.
5. Use only core dependencies (`sqlite3`, `libzstd`, `nlohmann/json`, test framework) as required. Do not install GUI libraries in Ravali's environment.
6. Build only one module and use modest compile parallelism to avoid cloud RAM/disk waste; clean generated artifacts when needed.
7. Record cloud usage budget/limits before creating persistent Codespaces; personal accounts have included usage allowances subject to plan, while organization billing can differ. Configure auto-stop and remove unused Codespaces to control cost.
8. If Codespaces cannot be used, switch to GitHub Actions builds or a permitted remote workstation **without changing Ravali's module assignment**.

**Verification rule:** A passed static editor/linter check is **not** equivalent to a compiled/tested C++ module. Ravali's packages require compiler/CTest evidence from a real local or remote toolchain. Build artifacts and test reports must be downloadable for the Week 16 integration PC.

## 24. GitHub workflow and merge-conflict controls

### 24.1 Repositories and branches

**Preferred:** one private repository with strict directory ownership and module-local builds. This gives a single source of truth for contracts and controlled release integration without duplicating code. Use:

```text
main                       protected; accepted integrated releases only
contracts/v1.0.0           immutable contract freeze tag (tag naming convention)
feature/vinay-desktop      Vinay's exclusive UI, DDC and license implementation
feature/ravali-core        Ravali's exclusive core, CLI and tests
integration/v2            limited Week 16+ merge/test branch
release/v2.0-rc1           jointly validated signed release candidate
```

Git branch names are illustrative. GitHub `CODEOWNERS` needs real usernames or teams entered by the maintainers, not fictional handles.

### 24.2 Pull-request rules

- **Main**: no direct pushes; protected branch; required independent build/test status checks and at least one approved review when supported by the repo plan/settings.
- **File ownership**: Vinay may only change his paths; Ravali may only change hers; shared files updated only through a joint PR. Configure `CODEOWNERS` to request appropriate reviewers.
- **No duplicate copies** of the same shared header or fixture maintained independently. Read from the frozen contract path/tag.
- **One user story per PR**, with ID (`V03`, `R05` etc.), acceptance checklist, dependency notes, passing tests and evidence file references.
- **No giant final merge without dry runs**: weekly compile/run contract suite against each developer's artifact, even while implementations remain on separate branches.
- **Do not merge secrets/vendor packages:** Git ignore for `build/`, generated traces, vendor SDK binaries, signed production entitlements, keys and sensitive bench logs.
- **Integrate once both module gates pass:** merge the validated two branches into `integration/v2`; solve interface defects in the owning module via separate PRs; final PR to main only after full end-to-end gate.

### 24.3 CI separation (concept, not existing automation)

| Check | Scope | Responsible | Required evidence |
|---|---|---|---|
| `contracts-compat` | Shared v1 fixture/schema/headers | Both (joint maintenance) | Fixed header/schema compile and golden fixture check |
| `core-linux-clang` | `modules/core/**`, `tools/core_cli/**`, `tests/core/**` | Ravali | Qt-free build+CTest; Clang warnings-as-errors where compatible |
| `core-linux-gcc` | Same | Ravali | GCC C++20 build+CTest, sanitizer in dedicated job |
| `core-windows-msvc` | Same, if available on CI | Ravali | C++20 MSVC portability smoke; no proprietary DDC dependencies |
| `ui-mock-windows` | Vinay-owned GUI files | Vinay | Qt Widgets+Qwt build, offscreen/gui state tests, mock sources |
| `ui-mock-linux` | Vinay-owned GUI files | Vinay | Qualified Ubuntu/RHEL-adjacent smoke; real RHEL tested separately |
| `bench-device-win-rhel` | Real hardware/license + approved drivers | Vinay + reviewer Ravali | Manually authorized bench evidence; do not run on hosted CI |
| `integration-full` | Assembled full app, real core+UI | Both | End-to-end fixture test, integrity, replay, export, memory, recovery |

CI jobs must not require licensed DDC SDK installations for the pure-core job. Build/test caches may be used with retention and data-handling controls; avoid uploading real/controlled bus capture as public CI artifacts.

### 24.4 Suggested PR description template

```text
Work package: V__ / R__
Owner:
Files edited (must stay in owned paths):
Interface contract version: contracts/v1.0.0
What changed:
Tests run locally/remotely, exact commands and exits:
Acceptance criteria passed:
Screenshots/logs/artifacts (redacted):
Known limitations, blocked real-hardware gates:
Reviewer signoff:
```

## 25. Module verification, evidence and acceptance

### 25.1 Vinay independent gate — `V-GATE`

- [ ] All 18 screens open in a **standalone mock UI build** and navigate correctly.
- [ ] License Gate does not expose unlocked analysis when simulated missing key; UI labels simulations accurately.
- [ ] All action buttons either have tested behavior or are visibly disabled and tracked as incomplete (no fake 'success').
- [ ] GUI thread never performs blocking SDK reads, recording, SQLite scans or expensive full data reloads.
- [ ] `QTableView` uses a virtual/bounded model; table memory does not increase linearly with session length.
- [ ] Qwt eight-signal chart supports pan/zoom/cursors with bounded display history; raw samples remain unaffected.
- [ ] UI metrics report measured p95 click latency, plotted FPS and RSS on reference PC (with test scenario and limits).
- [ ] Screen screenshots, UI regression transcript, QT/Qwt/compiler and device version report saved to `evidence/vinay`.
- [ ] DDC monitor-only configuration and transmit inhibit **actually verified on authorized isolated bench**, or explicitly marked BLOCKED.
- [ ] Physical dongle cryptographic check and hot removal **actually verified**, or explicitly marked BLOCKED.

**Pass rule:** `V-GATE-SOFTWARE` passes without DDC/key through mock mode; `V-GATE-HARDWARE` remains BLOCKED until real bench proof. The whole module is not release-ready until both pass.

### 25.2 Ravali independent gate — `R-GATE`

- [ ] `aerotrace_core` builds **without** Qt, Qwt, DDC SDK and license SDK, using GCC/Clang; MSVC smoke when configured.
- [ ] 1553 protocol, message types, mode code, malformed/partial records, wrap and timestamp provenance golden tests pass.
- [ ] ICD valid/invalid signed/unsigned/BCD/enum/multiword cases pass, including correct quality flags and no fabricated values.
- [ ] Raw file format version documented; `write -> finalize -> read` preserves all received raw data/metadata exactly.
- [ ] Crash/corrupt tail recovery preserves valid chunks, reports incomplete tail and never silently hides detected loss.
- [ ] Sparse session seek/search runs on 1M+ synthetic records without preloading all records into RAM.
- [ ] Replay sequence and decoded outputs deterministic with the same ICD revision, including bookmarks and error provenance.
- [ ] Export checksums, time bounds, data labeling, cancellation and partial failures verified.
- [ ] Synthetic 25k events/s sustained, 50k burst and ≥10MB/s recorder qualification attempted with observed results, configuration and explicit failures; 8h/24h endurance evidence as prescribed.
- [ ] CLI usage docs, CMake/CTest commands, CI links and sanitizer reports saved to `evidence/ravali`.

**Pass rule:** `R-GATE` requires tests with real compiler/CTest logs, not editor screenshots. Performance must be reported as measured values—not simply checked off because targets exist in this PRD.

### 25.3 Integration and release gate — `I-GATE`

| Gate | Joint test / observable result | Signoff |
|---|---|---|
| I01 | Qt GUI receives live synthetic record stream from real core, no mock dependencies | Both |
| I02 | Correct channel/Bus A/B, command/status, provenance, partial/malformed framing from DDC through core | Both, authorized bench |
| I03 | Record 1M+ generated events while switching screens/zooming/exporting; raw count and loss markers reconciled | Both |
| I04 | Capture→stop→reopen→seek→replay→ICD→plot→export yields deterministic documented results | Both |
| I05 | Physical key remove while capturing: stop request, bounded finalization, lock, no auto resume, interruption label | Both, physical key |
| I06 | Window close, forced kill, disk full, USB removal/reconnect and corrupt chunk evidence/recovery | Both |
| I07 | End-to-end RAM/CPU/latency: measure against Section 11 targets on Windows and target RHEL, report exceptions | Both |
| I08 | Installer on offline target system, DDC/Qt/Qwt/USB license dependencies validated and signed release manifest | Both |
| I09 | Physical monitor Tx-inhibit and no-BC/RT-transmission verified on approved test setup | Vinay executes, Ravali reviews |
| I10 | Retest contract v1 compatibility, license compliance, user docs, redacted diagnostic bundle | Both |

**Final release definition:** all P0s, V-GATE, R-GATE and I-GATE are PASS with referenced evidence. Any missing physical equipment, driver access, key vendor SDK, GPU/OS qualification or licensing legal approval is a BLOCKER—not a reason to label simulated tests as verified.

### 25.4 Proof format for every individual task

Each task should produce `evidence/<owner>/<task-id>/README.md` recording (1) git commit hash, (2) OS/compiler/dependency versions, (3) exact build/test steps, (4) expected result, (5) observed result, (6) pass/fail/blocked state, (7) screenshots or logs with redaction, (8) known limitations, and (9) reviewer initials. Avoid collecting confidential payloads unless explicitly authorized.

## 26. Merge-week runbook and defect responsibility

1. **Readiness freeze (end Week 15):** No contract changes except approved critical fixes. Confirm `V-GATE-SOFTWARE` and `R-GATE`, tagged module versions, fixture hashes, build manifests and logs.
2. **Create `integration/v2`:** Merge `feature/ravali-core` and `feature/vinay-desktop` via PRs. Resolve conflicts only through agreed owners; don't overwrite one person's work with another's copy.
3. **Assemble CMake:** Integrate `aerotrace_core` into desktop target; switch adapter implementation from `MockCorePort` to `RealCorePort` via CMake option/runtime injection. No user-visible feature should depend on mock code in the release target.
4. **Contract smoke:** Compile every port signature; ensure no undocumented vendor/Qt type crosses `contracts/v1`.
5. **Synthetic E2E:** Feed golden fixture source to assembled UI + core, save binary, seek/replay, validate ICD, error event, plot and export. Compare hash/record identity to golden values.
6. **Hardware bench:** Install DDC driver and key runtime only on approved machine; verify transmit inhibit with independent instrumentation and no active BC/RT configurations; never test on an operational bus without authorization.
7. **Security/lifecycle:** Test key removal with active recording, DDC disconnect, invalid signature, disk thresholds, startup without key, and resume only by deliberate re-arm.
8. **Performance/recovery:** Run 1-hour/8-hour and optional 24-hour soak, plot stress, SQLite search, export, accidental termination and integrity accounting, separately on reference Windows and RHEL images.
9. **Fix by ownership:** GUI, DDC, license, installer defect → Vinay; parser, ICD, recorder, replay, query/export defect → Ravali; contract bug → jointly versioned change. Re-run both independent suites after each patch.
10. **Release candidate:** Security/license legal review, driver compatibility guide, README, signed installer/RPM, checksums, test report and rollback/recovery instructions; review and approve by both.

**Conflict expectation:** This process minimizes overlapping file edits, but cross-module API errors, Git merge conflicts in shared files and last-minute driver issues can still occur. The weekly contract checks exist to detect them early without combining unverified work.

## 27. Traceability — who implements which product requirements/screens?

| Scope / PRD reference | Primary implementer | Verifier | Interface / dependency |
|---|---|---|---|
| LIC-001..003, WF-01/WF-08, S01/S16 | Vinay | Ravali (state/test evidence); physical key required | app controller → core safe-stop request |
| DEV-001..003, CAP-001 and physical CAP-003 metadata | Vinay | Ravali (captured fixture semantics), authorized bench | device adapter emits `RawRecord` |
| CAP-002 queue ingestion/recorder separation | Shared at seam: Vinay SDK producer; Ravali bounded recorder | Both | `IRecordSink`, backpressure/loss flags |
| CAP-003/004 protocol interpretation | Ravali | Vinay UI raw/decoded comparisons | `RawRecord` + status flags |
| ICD-001..003, WF-04 | Ravali decoding; Vinay editor UI | Both | ICD revision/schema + sample quality |
| REC-001..002, WF-06 core | Ravali | Vinay UI test and E2E | session and replay ports |
| UI-001..003, PLOT-002, UX-001 | Vinay | Ravali contract fixture verification | table snapshot/chart sample ports |
| ANA-001..002, alarms/quality | Ravali algorithms; Vinay visual screens | Both | events/signal snapshots |
| FLT-001 | Ravali query/filter logic; Vinay UI input | Both | filter DTO/async queries |
| REP-001 | Ravali data export; Vinay dialog/report UI | Both | export jobs and progress |
| DIAG-001 | Ravali core integrity counters; Vinay system/SDK diagnostics | Both | health snapshot DTO |
| SEC-001, packaging and platform installers | Vinay | Ravali release checks | signed builds, manifest |
| S02–S18 navigation and visual UI | Vinay | Ravali fixture correctness | mock and real core share contract |
| SQLite entities and ICD schemas, Section 14 | Ravali | Vinay integration contract review | on-disk migrations |
| Performance Section 11, release Section 16 | Both in own modules; both E2E | Joint | measurements vs targets |

## 28. Decisions and references for the new development plan

The unchanged product-safety requirements of Sections 1–16 remain the primary specification. Sections 17 and 21–27 add the two-person execution plan; they do **not** relax monitor-only operation, preservation of raw records, physical license checks, offline packaging, or evidence-quality capture requirements.

**External verification (checked 2026-10-25; confirm again at toolchain freeze):**

- DDC AceXtreme C SDK package and Windows/Linux driver/SDK matrix: https://www.ddc-web.com/en/databus/milstd1553-1/software-1/bu-69092
- Qt 6 Qt Widgets and CMake support: https://doc.qt.io/qt-6/cmake-get-started.html
- Qt 6 model/view best practices: https://doc.qt.io/qt-6/model-view-programming.html
- Qwt 6.3.0 and Qt 6 compatibility, Qwt License 1.0: https://qwt.sourceforge.io/
- Qt module and proprietary licensing terms: https://doc.qt.io/qt-6/licensing.html
- Browser-only `github.dev` **does not compile/run**: https://docs.github.com/en/codespaces/the-githubdev-web-based-editor
- GitHub Codespaces billing/storage and included plan quotas: https://docs.github.com/en/billing/concepts/product-billing/github-codespaces
- GitHub branch protections/CI gates: https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches
- GitHub file CODEOWNERS: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners

**End of AeroTrace 1553 PRD v2.0.** This document is a complete *proposed* product/development specification based on the supplied v1.0 PRD and 18-screen HTML design. No working SDK integration, production code, real-device benchmarks, valid physical dongle checks, or released native application is implied.
