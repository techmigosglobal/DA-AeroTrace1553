**AeroTrace 1553 0.5.5 — development assessment and proposed roadmap**

Prepared 2 November 2026 from this source bundle, its architecture graph, implementation, Designer forms, archived screenshots, requirements, and validation notes. This is a development proposal, not a new product specification or hardware qualification. Product code was not changed during the review.

Follow-up implementation and validation are tracked in [the development update](development-update-2026-11-02.md). This assessment describes the original review baseline.

**Recommendation**

Retain C++20, Qt 6 Widgets, Qt Designer, Qwt, CMake and Ninja. The strongest next investment is consistent interpretation and reproducible analysis of complete recordings. Develop storage, replay, timing diagnostics and linked investigation views before increasing the number of screens or adding more AI features.

Assume the present product remains an offline, passive, dual-channel MIL-STD-1553 desktop instrument. Windows is the demonstrated hardware platform; RHEL support requires its own vendor-driver and application qualification. Treat other bus protocols, active BC/RT operation and remote access as separate product decisions.

**How the application works**

The Qt-free core defines raw records and signal mappings, interprets command/status fields, performs deterministic engineering conversion, and reads/writes CRC-protected ATD recordings. The acquisition module adapts DDC legacy MT/MTI and the simulator to one source interface. During capture, a worker polls the source, preserves original packet bytes and normalized records, decodes signals, evaluates checks, and publishes bounded display batches and plot snapshots.

The application module coordinates profiles, a retained analysis archive, replay windows, time progression, signal series and exports. The desktop binds 18 Designer page bodies to navigation, hardware discovery, tables, plots, session operations, document workflows and diagnostics. The Python child process handles PDF/table extraction, local OCR, SQLite FTS5 retrieval and MiniLM ONNX embeddings. Optional prose generation uses a configured loopback endpoint; deterministic C++ conversion remains separate from model answers.

The current operator sequence is Connect devices → Start recording → Stop & save → Disconnect. Older architecture/PRD passages describe automatic recording; the November recording-fix evidence and current implementation supersede those passages.

**Existing strengths to preserve**

- Raw recording precedes display publication. An 8,192-record display queue can discard old display copies without intentionally discarding raw records. Hardware loss/overflow produces an incomplete session.
- Records retain raw ticks, tick resolution, host monotonic time, vendor status, command/status validity and source identity. Original DDC packets are also retained.
- The table retains 5,000 records; the analysis archive trims after 200,000; selected signal histories retain 8,192 samples, with up to eight traces published as bounded min/max envelopes. Profiles support up to 512 mappings.
- Conversion supports integer fields, signed values, floating point, BCD, word order, scale/offset and channel/bus constraints. Protocol interpretation already covers several BC/RT, RT-to-RT, broadcast and mode-command cases; it should be extended and qualified rather than replaced wholesale.
- Indexed replay loads bounded payload windows. Full-session paired raw CSV export already streams the recording and is not restricted to the GUI cache.
- ICD handling preserves source evidence and sends uncertain extraction to review. Local retrieval, profile validation, check logs, capture logs, diagnostic collection and test sources provide useful foundations.

**Evidence and its limits**

The November 2 recording-fix note reports 21 passing Windows CTest checks on the SES machine, a 600-second loopback receiving 6,041 messages, and six corrected-clock captures totaling 606 messages. The corrected CSVs reportedly had 100,000 microsecond successive deltas. The longer run preceded the clock-scaling correction and must not be used as a normalized time reference. No responding RT was configured, so no-response flags were expected. This is narrower evidence than complete protocol, timing, redundancy or endurance qualification.

The same note records an unexplained Windows 0x10D kernel failure during initial setup, followed by successful testing after reconnection. Its cause was not established. Preserve that investigation separately from application defects and include driver reliability in bench acceptance.

Archived hardware evidence belongs to another machine and its raw build/test artifacts are not included in this source ZIP. October documents also describe older SDK versions and an absent card. Consolidate a current compatibility matrix and link each claim to its dated evidence.

**Findings to address first**

| Priority | Finding | Effect and proposed correction |
|---|---|---|
| P0 | The event page and event CSV export call `metrics(false)` and then iterate `checkEvents`. That metrics mode deliberately excludes the events. | Check counts can rise while the table is empty and the CSV contains only its header. Request event data explicitly; add a UI-to-export regression using an actual generated check event. Evidence: `apps/desktop/pages/monitor_pages.cpp:331,367`; `modules/acquisition/acquisition.cpp:325`. |
| P0 | Engineering profile export and batch statistics route mappings with `command(record.commandWord)`, while live decoding routes with `interpret(record).payloadCommand`. | RT-to-RT payloads can decode live but disappear from reports because the payload belongs to the second command. Share one matching/decoding path across live values, replay, statistics and exports. Evidence: `modules/application/workbench.cpp:257`; `apps/desktop/services/task_services.cpp:73`; `modules/acquisition/live_decoder.cpp:47`. |
| P1 | Replay loads the latest `.profile.json`, although capture records revision boundaries in `.icd.jsonl`. | Historical values can be decoded with rules that were not active at capture time. Implement explicit “as captured” and “reinterpret with selected profile” modes with revision provenance in exports. Evidence: `apps/desktop/pages/session_pages.cpp:89`; `modules/acquisition/acquisition.cpp:187`. |
| P1 | `RecordingIndex` holds three 64-bit fields for every record and rescans on open. | Payload memory is bounded, but index memory/open time grow with total record count. At 100 million records, entry fields alone occupy about 2.4 GB before vector overhead/capacity. Add persistent sparse/chunk indexes and time/address summaries. Evidence: `modules/core/core.hpp:73`; `modules/core/core.cpp:227`. |
| P1 | Batch analysis explicitly limits each input to 32 MiB and loads a complete snapshot. | Normal long recordings cannot use the batch workflow. Extend the existing streaming scanner with immutable completed-session inputs, cancellation and incremental statistics/export. Evidence: `apps/desktop/services/task_services.cpp:58`. |
| P1 | Decoded numeric values use `double`, including 64-bit integer fields. | Large counters and identifiers can lose integer precision. Preserve exact signed/unsigned raw integers alongside floating engineering values; define serialization and precision behavior explicitly. Evidence: `modules/core/core.cpp:59`. |
| P2 | The topology page combines channels by bus label, uses recent command addresses and shows the first seven RTs per bus. | An addressed RT can appear to be an observed responding device, and External A/Internal A can be conflated. Use channel-scoped addressed/responded/error states and support all RT addresses plus broadcast. Evidence: `apps/desktop/pages/monitor_pages.cpp:301`. |
| P2 | Several rate/distribution/event series use only the last 5,000 retained records. | Their time coverage changes with traffic rate and may exclude display-skipped records. Compute live aggregates before display thinning and offline aggregates from the complete recording. Label every chart with its scope. Evidence: `modules/application/workbench.cpp:122`. |

The event finding is based on source inspection. The RT-to-RT export omission was also reproduced against freshly built backend libraries; details appear below. Passing existing tests does not demonstrate coverage of these cases.

**Storage and replay direction**

Preserve ATD1/ATD2 readers and original DDC sidecars. Introduce a versioned chunked recording format only with migration and recovery tests. Chunks should carry record bounds, clock domain/epoch, time bounds, checksums and compact routing summaries. Rotation should produce a session manifest linking all segments and sidecars. Separate normal stream flushing from an explicit OS durability policy; measure the throughput cost and document the possible loss window.

Use SQLite for session metadata, bookmarks, annotations, profile revisions, events and job history. Raw capture should remain sequential and independent of SQL commits. Make session identity stable across file renames. Publish a complete-session manifest only after required outputs are finalized, with recoverable intermediate states.

Prototype DuckDB plus Parquet for derived engineering datasets and cross-session SQL analysis. Benchmark a representative recording first. Use a pinned C API behind a small adapter, verify the chosen API/version and Windows compiler combination, and bundle required extensions for offline use. Keep derived datasets rebuildable from raw data, profile revisions and decoder version. Preserve integer nanoseconds, clock domain, resolution, quality and record identifiers in every derived row.

**Monitoring and analysis improvements**

1. Add a persistent per-channel/per-bus health strip: connection, recording state, last activity, message rate, hardware loss, display skips, recording capacity and session integrity. Distinguish connected-but-silent from an error.
2. Add RT/SA/direction statistics with expected versus observed periods, missing-message detection, inter-arrival distributions and percentile jitter. Separate SDK-reported facts from inferred retries or A/B failovers. Clock resolution and timestamp reference constrain what can be measured.
3. Build transaction-oriented inspection around the existing protocol interpreter. Preserve partial/unsupported transactions and vendor flags. Expand a golden corpus for responding RT traffic, RT-to-RT, broadcasts, mode commands, missing responses, bad word counts and redundancy behavior.
4. Extend signal rules with stale age, rate-of-change limits, stuck values, debounce and sequence checks. Use explicit units and a restricted expression grammar for derived signals. Add enums, bit labels, validity conditions, multiplexed layouts and piecewise conversions only as required by representative ICDs.
5. Provide cross-session comparison, selected-interval statistics, tolerance bands, annotations and reproducible reports. Separate sample-weighted and time-weighted statistics, and expose coverage/invalid counts.
6. Add spectral analysis only after sampling quality is characterized. Conventional FFT/PSD must not silently treat irregular, missing or cross-clock samples as uniformly sampled. Resampling and any alternative method must retain their assumptions in the report.

Bus utilization requires validated duration/gap semantics; message rate alone is not utilization. Independent channel clocks require measured alignment before correlated overlays. The current MTI configuration requests 2 microseconds per tick; nanosecond storage/display does not establish nanosecond measurement accuracy.

**Visualization and operator workflow**

Preserve the accepted neutral theme, native widgets, keyboard navigation and large monitor table. Improve the investigation flow: selecting an event or an interval should update the raw table, transaction inspector, engineering values and plots using stable session/record identity.

| Engineering question | Proposed view |
|---|---|
| What happened around this fault? | Shared time selection across event lanes, message timeline and stacked signal plots |
| Which RT/SA is noisy or missing? | RT-by-SA heatmap with separate counts, error rates and coverage |
| Is timing stable? | Inter-arrival histogram, percentile summary and jitter versus time |
| Did a state change? | Step plot or labeled digital lanes with explicit unknown/stale spans |
| What changed from the reference test? | Aligned comparison plots, difference trace and tolerance bands |
| How did traffic move? | Channel-scoped logical transaction timeline or matrix |

Add shared cursors and zoom within a validated clock domain, explicit unit axes, visible gaps/invalid spans, direct labels and non-color quality indicators. Support keyboard inspection and a table alternative. Fetch time-range data and multiresolution envelopes from disk when zooming; display downsampling must never become the input to numeric statistics. The archived plot screenshot demonstrates the value of a common interval: its slow state signal and faster signals currently show different time ranges.

Do not add electrical waveform or cable-topology claims from message records alone. Such views require compatible physical measurement hardware and evidence.

**Technology choices**

| Area | Recommended direction | Reason |
|---|---|---|
| Acquisition/conversion | Existing C++20 libraries and DDC SDK adapter | Direct hardware integration and existing deterministic contracts |
| Desktop and plots | Existing Qt Widgets/Designer and Qwt | Fits the installed application and technical 2D views; improve time-range querying before changing renderers |
| Metadata | Embedded SQLite | Local transactional catalog and annotations; select durability deliberately |
| Large offline queries | Benchmark DuckDB and Parquet | Column selection and predicate pushdown suit long engineering datasets |
| Documents | Existing isolated Python, PDF extraction, RapidOCR, ONNX and FTS5 | Preserve local evidence and schema validation; evaluate representative ICDs before replacing models |
| Advanced research analysis | Optional isolated NumPy/SciPy jobs | Reusable scientific methods without adding work to capture polling |
| Profiling | Tracy instrumented development builds | Measure polling, writes, decode, mutex contention and GUI refresh independently |
| Verification | CTest, Qt Test/model tester, compiler diagnostics, sanitizers and parser fuzzing | Strengthen current tests; prioritize protocol, corrupt-file, revision and model consistency cases |

Pin a known-good Qt/compiler/Qwt/DDC combination. Test upgrades as explicit compatibility changes. Add reproducible presets, dependency manifests and clean-build/package checks. This ZIP contains no Git history or CI configuration; establish the authoritative repository before parallel development. Keep licensed hardware SDK material outside public CI and run physical tests on a controlled bench runner.

Qt model mutations must remain on their owner thread. If profiling shows disk stalls or decoding delaying SDK polling, introduce bounded recorder/analysis stages while retaining a single SDK owner and explicit overflow behavior. Do not introduce extra concurrency without measuring the problem and testing shutdown/failure ordering.

**AI development**

Prioritize ICD corpus acceptance, extraction precision, citation correctness and an explicit “insufficient evidence” result. Improve lookup and reviewed correction workflows before adopting a larger model. If generation is later added, select local weights using a fixed evaluation set and target-PC memory/latency measurements. Keep answers distinguishable from validated mappings, and preserve the current separation between chat document choice and active conversion profile.

Useful later tasks include proposing a saved analysis query, explaining a deterministic check result, comparing two ICD revisions and drafting an evidence-linked report. Any proposed conversion change should be previewed and validated. Numeric results and alarms should remain reproducible without a language model.

**Proposed delivery order and exit criteria**

| Stage | Deliverable | Exit criteria |
|---|---|---|
| 1 — correctness baseline | Event publication/export fix, shared payload routing, documentation reconciliation and targeted regressions | A generated event appears and exports; RT-to-RT values agree across live/replay/report; current compatibility/evidence is unambiguous |
| 2 — recording and replay | Profile revision replay, stable session identity, streaming analysis, persistent sparse indexes, recovery/rotation design | As-captured replay is deterministic across a mid-session profile change; large inputs exceed 32 MiB without full payload loading; incomplete sessions remain visibly incomplete |
| 3 — monitoring diagnostics | Per-stream aggregates, timing/quality summaries and transaction corpus | Expected/observed counts agree against reference traffic; loss, silent streams, resets and redundancy transitions are represented truthfully |
| 4 — investigation workspace | Linked timeline/table/inspector/plots, state lanes and bookmarks | An engineer can trace an event to its raw record, mapping revision and plotted value, including after the live cache has evicted it |
| 5 — advanced analysis and release | Comparison/reporting, optional columnar cache and scientific jobs, expanded bench matrix | Reports are reproducible from raw evidence; performance and hardware acceptance targets pass on the declared machines |

Agree numeric targets before estimating calendar delivery: required recording duration and peak traffic, target PC/RAM/storage, supported SDK/card combinations, offline report latency, display-latency percentiles and acceptable durability window. Start endurance qualification with a representative 24-hour dual-channel recording, then extend to the actual operational duration. Include bursts, slow storage, low disk, cancellation, simultaneous OCR, unplug/reconnect and restart recovery. Compare expected and captured counts using a reference that can expose capture loss.

**Official references checked for the recommendations**

- [Qt model ownership and thread safety](https://doc.qt.io/qt-6/qabstractitemmodel.html)
- [Qwt technical plotting capabilities](https://qwt.sourceforge.io/)
- [SQLite WAL behavior and durability tradeoffs](https://www.sqlite.org/wal.html)
- [DuckDB client support](https://duckdb.org/docs/current/clients/overview)
- [DuckDB Parquet projection/filter pushdown](https://duckdb.org/docs/data/parquet)
- [Tracy profiler](https://github.com/wolfpld/tracy)
- [Qt model consistency tester](https://doc.qt.io/qt-6.12/qabstractitemmodeltester.html)
- [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [SciPy spectral-analysis methods and sampling considerations](https://docs.scipy.org/doc/scipy/tutorial/signal.html)

**Current review validation**

The Qt-free Release build completed with GNU 13.1.0, CMake and Ninja. The DDC adapter was deliberately disabled for this verification, and IPO was disabled. All six selected hardware-independent CTest checks passed in 5.91 seconds: core recording/replay, protocol interpretation/payload safety, signal checks/simulation, acquisition recording, bounded pipeline/conversions, and application services. This does not rerun or replace the archived 21-test desktop/hardware-compatible suite.

A temporary C++ reproduction linked the freshly built application, acquisition and core libraries. It supplied a valid RT-to-RT record from RT 3 / SA 7 to RT 5 / SA 2, with matching status words and a one-word value of 1200. The transmitter mapping decoded correctly, but profile engineering export silently emitted zero rows:

```text
protocol_valid=1 live_decode=1200 engineering_export_rows=0 error=
```

Build, CTest logs and the temporary reproduction are outside the project at `C:/Users/Vinay/AppData/Local/Temp/AeroTrace-review-20261102`. The CTest log is `Testing/Temporary/LastTest.log` beneath that directory. The reproduction returns success when it demonstrates the omission; it is evidence of a defect, not a passing product acceptance case.

GUI, physical card and document runtime checks were not rerun as part of this assessment. The assessment document is the only intentional project-file addition.
