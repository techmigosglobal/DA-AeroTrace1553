# AeroTrace 1553: 0.5.0 versus 0.5.5

Comparison prepared on 2 November 2026 from the supplied local files.

**Overall assessment:** 0.5.5 is primarily a hardware compatibility, recording correctness and Windows deployment update. It is the stronger candidate for continued development on the tested Windows/DDC configuration. The main analytical features were already present in 0.5.0; this release makes the path from device connection to usable recorded evidence more dependable.

**What was compared**

- Baseline: `C:/Users/Vinay/Downloads/aero trace/aero trace`. Its CMake project declares 0.5.0, and the existing `out/build/desktop-release/apps/desktop/aerotrace_desktop.exe` reports ProductVersion and FileVersion 0.5.0.
- Target: `C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source.zip`. Its source declares 0.5.5, and the supplied installer reports ProductVersion 0.5.5.
- Both supplied SHA-256 sidecars match their ZIP/installer files. This verifies consistency with the supplied checksums, not publisher identity or code signing.
- Comparing all 1,030 files in the source archive against corresponding baseline paths found **36 changed, 8 added and 986 unchanged**. Recognized text files were compared after normalizing Windows line endings. In the application, module, script, tool, documentation, example, installer and bundled-source scopes checked, baseline-only files were Qwt Git metadata, rather than application features removed from the target.
- The baseline is a local source snapshot without a root Git repository. This is a comparison of those supplied snapshots, not a reconstruction of every intermediate 0.5.1–0.5.4 release.
- The codebase-memory index was consulted for structure. Its October 29 index had stale/partial coverage, so conclusions use direct source reads and file diffs.
- No installer was run and no hardware test was performed for this review. Historical test results below are attributed to the validation notes included in the target ZIP; the original SES machine's test logs, CSVs and minidump were not independently available here.

**Feature and functionality comparison**

| Area | Local 0.5.0 | Supplied 0.5.5 | Practical development benefit |
|---|---|---|---|
| Hardware connection | Connection and capture startup are coupled. Automatic startup/retry paths can begin hardware capture and recording. | Explicit **Connect devices** initializes the selected channels without starting acquisition or creating a recording. | Device readiness can be checked before a test run begins. |
| Recording controls | Capture startup owns opening the source and beginning acquisition. | A separate **Start recording** button in Live Monitor starts receiving and saving. Launch, device arrival and configuration changes do not automatically record. | Clearer test boundaries and fewer unintended recordings. |
| Repeated recording | Stopping through the old acquisition stop path closes the source. | **Stop & save** finalizes the recording and CSVs but retains a healthy connection. Subsequent runs reuse it. | Less repeated device initialization during iterative bench tests. No measured startup-speed gain is established. |
| Disconnect | Stop/release is part of the capture lifecycle. | **Disconnect devices** saves an active run before releasing the card. Reconnection waits for another explicit recording action. | Distinct connected, recording and disconnected states are easier to operate and test. |
| DDC runtime selection | Defaults to the packaged DLL or a build-time SDK path. | New runtime selection searches installed SDK locations, environment/registry evidence and packaged/build candidates. It checks x64 architecture, exports and version evidence. Conflicting runtime versions can require explicit selection. | Better portability between development and bench computers; clearer failure information. |
| Logical channel selection | Windows discovery and manually configured logical IDs already exist. | Can suggest an unambiguous local two-channel Card Manager pair matching the present device. Saved settings are retained. | Reduces initial setup work without assuming every registry assignment belongs to the connected card. |
| SDK layout support | Defaults to the SDK 4.10.2 source/library layout used on the baseline PC. | Supports the older `Include` / `Lib/x64/Release` layout as well as the newer `libraries/emacepl` layout. | Enables the tested SES installation with SDK 3.6.15 while retaining the alternate layout. This is not universal ABI compatibility. |
| Empty/no-data packet reads | Explicitly tolerates unavailable/busy buffer results. | Also tolerates SDK data-unavailable and timeout results. | An idle bus can remain connected without these normal conditions becoming capture errors. |
| Packet release | Treats nonzero packet-release results as errors. | Handles the SDK 3.6.15 Windows caller-owned-buffer `-68`/not-supported case. Other release failures, including SDK-owned-buffer failures, remain errors. | Resolves a specific capture interruption while preserving meaningful failure reporting. |
| MTI timestamp conversion | Stores a 100 ns multiplier even though the requested native timebase is 2 microseconds. | Uses **2,000 ns per native tick** for the relevant DDC MTI path and updates its diagnostic metadata/tests. | Corrects time axes, intervals, replay timing and downstream rate calculations for affected new captures. |
| Paired session CSVs | Stop/save uses the rich `aerotrace.raw-csv.v3` schema, with 77 columns and lowercase `_ext.csv` / `_int.csv` suffixes. | Stop/save uses `aerotrace.vendor-session-csv.v1`, with exactly **42 named columns** and default `_EXT.csv` / `_INT.csv` suffixes. | Matches the supplied EXT/INT sample structure, simplifying integration with that workflow. Existing consumers need schema updates. |
| CSV contents | Includes record IDs, timestamp/provenance fields, protocol details and per-word hexadecimal columns. | Includes card time, per-channel microsecond delta, block status, message direction, bus, interpreted receive/transmit commands and statuses, word count and 32 data-word columns. | More directly usable in the target bench spreadsheet/report format. Rich raw CSV generation remains implemented separately. |
| Export retries | Already checks file ownership/hashes and protects unrelated files. | Completed unchanged exports return their existing result without rewriting CSVs. Changed recordings are rejected while preserving the published pair. Directory collisions are also checked. | More predictable retry behavior, including when another application has a CSV open. |
| Qt/Qwt build portability | Build scripts default to Qt 6.12.0 and an existing Qwt binary location. | Scripts can find an installed Qt 6 MinGW kit, build Qwt for that kit, and use `desktop-local` / `qwt-local` outputs. | Reduces dependence on copied caches and another computer's prebuilt libraries. Compiler/tool paths still have defaults. |
| PDF runtime selection | Primarily assumes one source-tree Python environment. | Checks configured/local/installed Python candidates for required packages, with matching desktop and packaging changes. | Fewer document-feature failures caused by a missing or moved development environment. Extraction intelligence itself is unchanged. |
| Windows installation | Installer source uses a per-user LocalAppData directory and requires no elevation. | Installer source targets 64-bit Program Files, requires administrator installation and adds a machine App Paths registration. | More controlled deployment on shared lab machines; installation now requires admin access. |
| Packaging and support | Offline packaging, hashes, dependency checks and diagnostics already exist. | Packages the runtime from the configured SDK, records Qt/runtime provenance, refuses a nonempty package destination, improves registry diagnostic serialization and adds source/release scripts. | Better traceability between tested dependencies and shipped files. |
| Automated regression coverage | Existing capture, document, protocol, session, export and GUI tests. | Adds a GUI connection/recording lifecycle test and expands lifecycle, runtime rejection, timestamp and CSV checks. Release notes report **21 CTest tests passing**. | Covers the changed behavior at service and user-interface levels. Historical test counts vary across documents, so a clean percentage increase over 0.5.0 is not established. |

**The timestamp correction matters beyond the display**

In the affected old MTI path, 50,000 ticks multiplied by 100 ns becomes 5 ms. At the corrected 2,000 ns per tick, the same interval is 100 ms. The old normalized interval is therefore **20 times too small** for this timebase. A frequency computed from it would be 20 times too large.

This is a scale correction, not a claim of finer physical resolution. A microsecond-formatted display does not establish microsecond hardware accuracy. Legacy MT already uses a 2 microsecond conversion in the baseline; the multiplier change should not be applied indiscriminately to every source.

The recording reader and replay implementation are unchanged between these snapshots. Do not assume upgrading automatically repairs historical ATD timestamps. Retain old recordings and packet sidecars, identify affected MTI captures by provenance/configuration, and design an explicit, verified conversion if old data must be reused. Simulator, legacy MT and already-corrected recordings need separate treatment.

**The CSV change is an integration change**

The new session header is:

```text
DateTime,DeltaT,BlockStatus,MessageType,Bus,Rx_Cmd,Tx_Cmd,Tx_Status,Rx_Status,WordCount,DataWord_0,...,DataWord_31
```

The first ten fields plus 32 data words total 42. The actual header spells out every data-word field; the ellipsis above is explanatory shorthand.

- `DateTime` is formatted card time such as `00:00:48 745.527`; its name does not make it a calendar date or UTC timestamp.
- `DeltaT` is in microseconds and follows the preceding record on the same exported channel, including A/B bus interleaving. The first row is zero; backward clock movement can produce a negative delta and is counted in the manifest.
- Hexadecimal words are lowercase. Unused data-word cells contain `-`; absent status fields remain blank.
- Commands include interpreted addressing/direction text, for example `0947 --> 01-R-10-07`.
- The seven unnamed trailing fields in the supplied examples are intentionally excluded, according to the correction note.
- Completed-session export still covers the full recording irrespective of display filters. An unused counterpart channel still produces a header-only CSV. Those are retained capabilities, not new inventions in 0.5.5.
- The narrower session CSV does not expose every provenance/protocol field in the old 77-column output. Preserve ATD files, original packet sidecars and manifests when that evidence is required. The detailed raw exporter remains in the code.

Update consumers to read the declared schema and field names, rather than fixed old positions. Review filename assumptions, blank/unused values and microsecond units. Existing published session descriptors can retain their previously recorded filenames; the uppercase suffix is the default for new pairs.

**Capabilities already present in both versions**

Direct comparison found the relevant implementation files unchanged for these capabilities:

- C++20, Qt Widgets/Designer, Qwt and the core/acquisition/application separation.
- Eighteen primary screens, the existing visual theme, tabbed/movable channel views and channel/bus/RT display filtering.
- Raw recording without an ICD, ATD recording/replay, indexed replay and its controls.
- PDF extraction, local OCR, source-page evidence review and JSON ICD profiles.
- Local document retrieval, ICD Assistant conversations and the optional separately hosted loopback language-model endpoint.
- Engineering conversions, up to 512 mappings, up to eight active plotted signal subscriptions, history/downsampling and cursor behavior.
- Existing signal checks, saved task workspaces, payload simulation and batch analysis/reporting.
- Existing protocol interpretation and optional licensing/key-gate implementation.

Consequently, 0.5.5 does not establish improved OCR accuracy, a smarter language model, expanded mapping/trace limits, new replay intelligence, a new design, full protocol certification or new physical-key qualification. Its document changes improve runtime availability, rather than the extraction/retrieval algorithms.

**What the supplied test evidence establishes**

The correction and release notes report the tested physical combination as Windows 11, Qt 6.11.1 MinGW x64, aceXtreme SDK 3.6.15, acexusb driver 3.6.15.1 and a BU-67113U USB card.

They record:

- A 600-second loopback capture with 6,041 received messages and no reported hardware loss, overflow or `-68`. This run occurred **before** the clock correction and supports receive stability, not corrected normalized timing.
- After the correction, three 10-second runs in each direction, with 101 messages per run: **606 messages** total across six captures.
- Twelve paired CSV files checked for 42 fields, expected payload/direction and **100,000 microsecond** successive intervals. The inactive channel files contained headers only.
- Repeated recording without reopening the connection, plus installed-app launch, GUI, PDF, export and diagnostics workflows with a restricted PATH.
- Final release notes report 21 CTest passes and a Program Files install/uninstall round trip, including generated Python-cache cleanup and executable/manifest version checks.

This is a substantial advance over the baseline's documented missing-card/registry-open failure. It is still bounded evidence: the loopback did not configure a responding RT, and it does not validate all message types, both buses under simultaneous load, all SDK versions, cross-device synchronization or indefinite operation. There is no comparable benchmark here that proves faster rendering, higher maximum throughput or zero lag. One concurrent-document test threshold was lowered from 100,000 to 50,000 records, which is another reason not to infer improved performance from test success alone.

The correction note also records a Windows 0x10D blue-screen during initial setup. Later runs passed after USB reconnection; the cause was not established. Treat it as an unresolved observed reliability issue when planning longer hardware qualification, rather than claiming the release fixed it.

**Recommended development direction**

1. **Use 0.5.5 as the candidate baseline for the tested bench configuration.** Its fixes address acquisition startup, repeated runs, timing correctness and shipment consistency. Preserve the existing 0.5.0 snapshot as a reference.
2. **Migrate CSV consumers and test procedures together.** Adopt Connect → Start recording → Stop & save → Disconnect, and validate the new schema against representative customer tools before switching routine output.
3. **Establish a timing reference.** Repeat captures with known traffic intervals and compare card time, raw ticks, exported deltas and replay. Keep pre-correction captures identifiable.
4. **Make builds reproducible.** Specify the intended Qt/compiler/SDK combination rather than relying solely on highest-installed-version discovery. Rebuild Qwt when changing kits; use fresh build outputs. Keep acceptance results tied to package hashes and actual runtime-selection diagnostics.
5. **Extend hardware acceptance where it remains weak.** Add representative responding-RT traffic, bus B, simultaneous channels, sustained/high-rate bursts, unplug/reconnect, clock reset/rollover and longer runs. Investigate the recorded kernel restart using its original evidence.
6. **Reconcile the documentation before the next delivery.** Several guides and status files are byte-identical to the older baseline. The target README still opens with a no-connected-bus statement and references an intermediate installer. The later correction/release notes report loopback success and the final 0.5.5 Program Files installation. Update those documents to one current workflow and qualification statement.

Remaining roadmap items include representative ICD acceptance, broader protocol/timing analysis, physical licensing acceptance, RHEL qualification, huge-session/rotation/power-loss behavior and release signing. No implementation change in the compared snapshots establishes those as completed by 0.5.5.

**Local evidence for this review**

- [Complete file inventory](full-source-inventory.json) and [text/source change inventory](source-changes.json).
- [Recording/CSV validation note](0.5.5/.codebase-memory/RecordingFix-validation-20261102.md).
- [Final 0.5.5 release note](0.5.5/.codebase-memory/Release-0.5.5.md).
- [Earlier SES validation note](0.5.5/.codebase-memory/SES-validation-20261102.md).
- Text diffs under `out/version-comparison/diffs/` and selected extracted source under `out/version-comparison/0.5.5/` support the feature comparisons above. This extraction is review evidence, not a complete buildable checkout.

Source ZIP SHA-256: `a515776f7639825cf4a147e825349a4f2f6d086a7deb2d99caed2a3d363b2164`.

Installer SHA-256: `14d2f0c577f29cbc7827e584572b63a38a5f6e14b26cd1df7110c4ded34ac103`.
