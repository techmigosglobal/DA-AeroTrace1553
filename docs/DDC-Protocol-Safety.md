# AeroTrace 0.5.0 — DDC reliability and protocol interpretation

Current operation and qualification status: [0.5.5 status](current-status.md). Older machine-specific results below are historical evidence; they do not describe current hardware presence or qualify later builds.


This update strengthens receive-only monitoring. It does not qualify physical operation, transmit as a BC/RT, install drivers, change vendor assignments, or update firmware.

## Windows readiness and error -82

The local inspection found Windows driver-store packages 4.10.2.1, including E2MA USB (`e2mausb.inf`). Windows reported no connected DDC-class device. The vendor registry contained numbered empty slots, not populated logical assignments. These observations do not establish the cause of the earlier SES driver-open failures.

Device setup distinguishes runtime loading, Windows presence/binding, populated vendor assignment candidates, SDK initialization, and received traffic. Empty slots no longer count as assignments. Headers identify `DeviceID`, `Channel`, and `Family`, but do not establish registry representations or channel numbering; raw values/types are displayed without guessed mappings. An incomplete enumeration means presence is unknown.

For E2MA USB, verify `USB\VID_4DDC&PID_5590`, service `e2mausb`, the installed INF, PnP problem code, and Card Manager assignments. Logical numbers are 0–31, as defined by the SDK, regardless of additional registry slots. Driver-store staging alone does not establish that a device is connected or accessible.

Error -82 means the SDK could not open its device driver. The diagnostic includes the requested logical number and runtime. Missing/mismatched binding, unconfigured assignment, access restrictions and another application's ownership are checks to perform, not established causes. Compare the Windows evidence, Card Manager identity, SDK operation results and session logs from the same computer/run.

## SDK ownership and integrity

One capture worker owns SDK calls. It first opens passive MT identity, queries hardware capabilities, and requires the function group for classic MT or MTI. A legacy-capable runtime is not rejected merely for missing MTI entry points. Runtime rescans are blocked during hardware connection/capture/finalization, and hardware connection waits for an in-progress scan, so runtime inspection cannot overlap capture SDK ownership.

E2MA/EMACE uses classic MT with original 40-word records retained in `.mt40`; capable devices use MTI with original Chapter 10 packets retained in `.ch10`. Runtime loading is not physical communication. Start/stop logs name the actual MT or MTI operation.

Stop & save stops producers, drains available buffers, obtains final supported MTI integrity snapshots while handles remain open, and closes the SDK. Disconnect/error shutdown also attempts bounded recovery of available records. Stop, read, packet-release, free, or recording errors label the session incomplete. The original failure remains in diagnostics alongside cleanup errors.

Per-channel MTI dropped/overflow raw snapshots are logged. Displayed aggregate counters are observed high-water evidence, not certified lifetime totals: available SDK headers/examples do not establish cumulative/reset-on-read semantics. Classic read-loss evidence is retained separately. Validate real final-buffer behavior on each model; SDK fakes and vendor examples do not prove transfer of a partially filled physical stack after stop.

Hardware identity logs preserve model, family, serial, firmware/HDL raw values, driver and capabilities. Vendor release notes identify BU-65590U/BU-65591U support and a minimum firmware entry of 33. Confirm the model-specific version encoding with Card Manager/vendor documentation before comparing that requirement. AeroTrace performs no firmware update.

## Transaction and engineering safety

The common interpreter covers BC-to-RT, RT-to-BC, RT-to-RT, broadcasts and defined mode commands. Hardware SDK message type, command/status presence and participant addresses must agree. A zero-valued RT0 status can be present and valid. RT-to-RT command1 identifies the receiver; command2 identifies the transmitter and payload mapping. Status1 belongs to the transmitter and status2 to the non-broadcast receiver.

Mode subaddresses 0/31 use a mode-code field, not an ordinary word count. The interpreter checks the assigned mode, permitted T/R direction, broadcast option and zero/one-word payload. Reserved or unknown types retain their raw data but are not converted.

Status descriptions separate Message Error, Instrumentation, Service Request, reserved bits, Broadcast Received, Busy, Subsystem Flag, Dynamic Bus Control Acceptance and Terminal Flag. Informational bits do not automatically invalidate payloads. Busy, fault status, missing expected responses, inconsistent commands, capture errors and payload-count mismatch prevent engineering conversion. Inspection and engineering tables explain the rejection. Raw records remain exportable.

Wire status masks are distinct from the SDK's RT configuration masks. Generic invalid-word evidence is not reported as a proven parity-only failure.

## CSV and older recordings

Raw CSV schema `aerotrace.raw-csv.v3` retains the existing 60 columns in position and appends transaction, transmitter/receiver RT, mode, expected/actual payload counts, eligibility, reasons, status meanings, presence, SDK type and timing/evidence fields. Existing direction/RT/SA/quality labels now use shared protocol interpretation; RT/SA identify the payload route. For mode commands, ordinary `word_count` is blank and the appended mode/payload fields carry the meaning.

Primary paired exports remain UTF-8 with BOM and spreadsheet-safe `0x` columns. Exports read the finalized CRC-validated recording and are independent of display filters, retained rows and analysis limits. Sessions with capture loss or shutdown failures remain incomplete even if all available records export successfully.

ATD1/ATD2 originals are not rewritten. Simulator/ATD1 records without SDK evidence use inferred interpretation, explicitly labeled in CSV. Missing hardware evidence is not reconstructed as a measured fact. Unsupported transactions leave interpreted mode/count fields blank rather than presenting zero as a known value. Existing raw-column plots use the same payload RT/SA as the table; compound directions and mode codes are not plotted as ordinary binary direction/word-count values.

## Timing limits

The current DDC MTI configuration requests 2 µs native ticks and preserves 2,000 ns resolution. Do not scale these packets with a generic 100 ns assumption. Classic tags preserve their 2 µs resolution and silent-rollover ambiguity.

The SDK declares two one-byte Chapter 10 gap fields, but the inspected headers/examples do not establish units, saturation or measurement-point semantics. Their original bytes remain in raw packets. Response time and intermessage-gap verdicts therefore read **not measured**. Consecutive message timestamps are not a substitute, and separate card clocks are not assumed synchronized.

## Acceptance evidence

Automated checks cover protocol fixtures, SDK fakes, complete export beyond 200,000 records, replay and GUI behavior. The long-run harness supports a 30-minute 50,000-record/s simulation, private-memory samples, UI heartbeat percentiles, CRC scanning and complete paired CSV counts. It requires an effective rate of at least 49,000 records/s (2% tolerance), p95 heartbeat below 100 ms, and post-warm-up private-memory growth below 128 MiB; maximum heartbeat delay is recorded separately. Record actual results in the validation receipt; a configured harness is not a passed run.

CRC generation/validation uses a table with the same IEEE CRC32 polynomial and byte order as existing recordings. Known vectors, an independent bitwise implementation and original recording comparisons verify compatibility. Engineering conversion shares one interpretation per incoming record rather than recalculating it for every mapped signal. CSV integers and hex cells use locale-independent append formatting for large exports. The desktop export service is also exercised by the internal `aerotrace_session_export_cli` validation helper to retry a retained long-run recording without recapturing.

Physical qualification remains separate for this PC and SES. Follow the SES bench checklist on both machines and retain model, firmware, driver/INF, runtime hash, logical mappings, known-source traffic, counts, sampled raw words, loss evidence, Stop & save, restart/replay and reconnect results. Mark only the tested configuration verified.

## Delivered software validation

The final development executable SHA-256 is `F329B0511EB62BDCAE0201C7869A85BB50700B60B0E808BA4B770691CF3725D3`. All 20 automated tests passed, including protocol fixtures, SDK fakes, GUI parity and the 220,008-record export/replay check.

The final 30-minute simulation passed capture and complete paired export: 90,007,435 CRC-valid records, 45,003,718 External CSV rows and45,003,717 Internal CSV rows. Effective throughput was 50,004 records/s. UI heartbeat p95 was 16 ms; the worst delay was 1,190 ms, so occasional UI stalls remain. Post-warm-up private-memory growth was 4,845,568 bytes. Logging reported no failure; the display queue remained bounded at 8,192 and 100,912 display skips did not remove records from disk/export.

The earlier long run retained 71,538,540 valid records but missed throughput acceptance at approximately 39,744 records/s and its export process ended before publication (cause unverified). It is retained as an unsuccessful baseline, not counted as a passed soak. CRC, repeated interpretation and CSV formatting costs were reduced before the final run.

Evidence: `out/verification-0.5.0-ddc-protocol.json`, `out/validation-0.5.0-ddc-protocol-final.log` and `out/ddc-protocol-soak-final-30min/performance.json`. Final raw/CSV validation artifacts are under `D:/AeroTrace-Validation/DDC-Protocol-0.5.0-Final-20261031`. This is software/simulation evidence. Hardware is unavailable; local and SES physical acceptance remain pending. Installed SDK runtime/INF hashes and original 0.4.0 recording/CSV hashes were checked unchanged.
