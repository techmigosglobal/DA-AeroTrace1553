# Current behavior and evidence — 0.5.5

Updated 3 November 2026. This page and the current Operator Guide describe operation. Dated validation notes describe the machine and build tested then; they are not live device inventory.

## Operation

1. Select the DDC source, real Card Manager logical assignments and enabled paths. External / CH1 and Internal / CH2 are role labels; A and B are redundant paths within each channel.
2. **Connect devices** configures selected channels without receiving or creating a recording. Launch, device arrival and configuration edits never automatically record.
3. **Start recording** explicitly starts receiving and writing. Display filters do not restrict disk capture. A/B can be enabled on both channels; simultaneous physical qualification remains required.
4. **Stop & save** stops/drains, finalizes and exports paired `_EXT.csv` and `_INT.csv` files. Successful stop retains the connection; failed recordings are marked incomplete.
5. **Disconnect devices** saves an active run and releases hardware. After device loss, explicitly reconnect/start a new session. No simulator fallback or automatic recording.

Raw recording, replay and paired full-session CSV require no ICD. Engineering conversion requires validated mappings. Replay uses a bounded payload window and a per-record in-memory index. Batch analysis still limits inputs to 32 MiB. Replay currently uses the latest profile snapshot rather than reconstructing revision history.

## Latest supplied physical evidence

The [November 2 recording-fix note](../.codebase-memory/RecordingFix-validation-20261102.md) reports Windows 11, Qt 6.11.1 MinGW x64, SDK 3.6.15, acexusb 3.6.15.1, BU-67113U:

- A 600-second, 6,041-message receive-stability loopback. This preceded clock correction; normalized times are not a clock reference.
- Six corrected-clock 10-second captures in both channel directions, totaling 606 messages. All 12 paired CSVs were checked for 42 fields and 100,000 µs successive deltas in populated streams.
- No responding RT was configured. Bus B, all four streams concurrently, responding-RT coverage and extended endurance remain unqualified.
- A Windows 0x10D kernel failure occurred during setup; cause unresolved. Later successful capture does not close that investigation.

The [0.5.5 release note](../.codebase-memory/Release-0.5.5.md) reports 21 passing tests and machine installation under Program Files on SES. Raw archived evidence is outside this ZIP. Modified builds require fresh verification.

## SDK and time semantics

Runtime discovery checks x64 architecture, installed/manual locations and complete receive exports rather than one version number. MT and MTI are reported independently; MTI requires packet release and loss metrics. Hardware capability selects the mode; missing interfaces cause explicit errors.

An application-specific SDK folder or DLL takes priority, followed by DDC_SDK_ROOT, then automatic installed-SDK discovery and packaged/build fallback. Invalid explicit configurations fail instead of selecting a different runtime. See [per-PC SDK configuration](sdk-compatibility.md).

Export matches do not prove structure layouts, calling conventions, driver/firmware compatibility or time semantics. Unknown ABI families need a separately built adapter and bench validation. Arbitrary future SDKs cannot be guaranteed by discovery. Preserve runtime path/hash/API, compile-time SDK identity and actual device/driver evidence.

The adapter requests ACE_TT_2US: native MTI ticks represent **2,000 ns**, not 100 ns. Raw ticks/bytes remain intact. Display precision does not establish accuracy or cross-channel synchronization.

## Development priorities

[Hardware qualification](SES-Bench-Checklist.md) is the first release gate. [The approved RT monitor design](design/rt-live-monitor-proposal.md) is implemented in the development source. External/Internal panes default to RT grouping, with independent A/B latest values and counts, expandable message routes and a chronological mode. Aggregates are computed before raw display thinning; pausing RT display never stops recording.

Keep C++20, Qt Widgets/Designer/Qwt and isolated Python documents. SQLite session metadata, a DuckDB/Parquet benchmark, NumPy/SciPy analysis and development profiling remain separate stages, not features claimed by this update.

The [2 November development update](development-update-2026-11-02.md) records an earlier baseline: all 25 software checks passed across a full run and one corrected test-setup rerun. That build used the native plot fallback. The newer qualification build uses Qt 6.12.0 with a matching local Qwt build and an expanded test inventory. No connected DDC card was available, and no installed release was replaced.

The [earlier 3 November readiness audit](development-readiness-2026-11-03.md) distinguishes existing OCR/ONNX/document SQLite from the unimplemented session catalog, DuckDB/Parquet and scientific jobs. The local model/sanitizer/fuzz tooling has since been added. CI/CD remains intentionally unconfigured. Lag-free physical operation has not been established.

## Provider and verification development — 3 November

The source now contains an in-process C ABI provider DLL boundary, runtime/header fingerprints and a local build recipe for additional SDK families. See [provider contract](provider-contract.md) and [local verification](local-verification.md). The installed 4.10.2 SDK supplies the current provider build; licensed 3.6.15 inputs remain unavailable. Physical qualification stays NOT RUN. Final software run results are recorded separately from the earlier validation baseline.
