# AeroTrace 1553 V0.6.0

Reported 4 November 2026. Software qualification: **PASS** for the available inputs and stated gates. Full preflight: **BLOCKED** by unavailable licensed SDK 3.6.15 inputs. Physical qualification: **NOT RUN**. CI/CD: **NOT CONFIGURED / NOT RUN**, as requested.

[Final installer and staged-payload validation](Packaging-0.6.0.md) records the delivered V0.6.0 artifact, checksum, source identities and acceptance scope.

## Delivered scope

- Approved External/CH1 and Internal/CH2 RT panes, stable RT rows, independent A/B values and counts, expandable routes and chronological display. Raw capture remains independent of display thinning.
- Versioned, Qt-independent C ABI provider DLLs, bounded ownership and lifecycle handling, runtime/header fingerprints, structured diagnostics, explicit rejection of unsupported combinations and recipes for additional SDKs.
- Production providers compiled and checked for aceXtreme SDK 4.10.2 (runtime 4.10.1.1) and the supplied 4.4.2.4 package. Both coexist in the installer. An installed 4.4.2.4 runtime is selected explicitly or discovered with its matching provider; the package default runtime is 4.10.1.1. No licensed vendor headers or runtime binaries are published in source control.
- Deterministic four-stream stress, passive bench collector, exact per-stream comparison, separate stage timing, bounded queues/traces, native preflight, Qt model checks, ASan/UBSan and production parser fuzz targets.
- Dedicated bounded application journal, capture correlation and support ZIP. Existing isolated document Python/OCR/ONNX, retrieval models and document SQLite cache are included.
- One Windows x64 offline installer with private Qt/Qwt and document runtimes. No developer compiler, native verification toolchain or separate Python installation is needed to operate it. The deployment marker disables provider fallback into developer build directories.

Session SQLite revisions/annotations/catalog, DuckDB/Parquet analysis and isolated NumPy/SciPy scientific jobs are deferred by the user's explicit choice to package the completed update now. Document-runtime NumPy is a dependency, not a scientific-analysis user workflow. Tracy integration is not claimed. No CI/CD workflows were added.

## Build identity and evidence

The full functional qualification ran from clean commit **b58863506cc1768f66f6be765d134cbc2235b424**, Qt 6.12.0 MinGW x64 with Qwt. Its executable SHA-256 was **A475088A7B4EC1755F9D07A9A2EED4A099E4A0E375D88F50BF68407B64CB7C3D**; the version label at that time was 0.5.5. V0.6.0 version metadata, deployment isolation and installer changes were built and checked separately. The ten-minute soak was not rerun against the relabelled installer executable. The packaged binary identity is in deployment/acceptance receipts.

[Machine-readable qualification receipt](Qualification-0.6.0.json) includes stage distributions, stream totals, fuzz executable identities and SHA-256 hashes of local evidence. Raw captures, machine-specific paths, licensed inputs and multi-gigabyte CSVs remain in ignored local evidence folders. Evidence path references are local provenance, not public downloads.

| Check | State | Evidence / result |
| --- | --- | --- |
| Desktop regressions | PASS | 29/29; out/preflight-final3-20261103/desktop-regressions.log |
| Release deployment/provider checks | PASS | 4/4 including new provider_deployment_isolation; out/release-0.6.0-final-build.log |
| Native backend ASan/UBSan | PASS | 14/14 including actual sanitizer detection canaries; out/scanner-native-regressions.log |
| Final CSV formatter sanitizer regression | PASS | application_services rerun; out/csv-sanitizer-regression.log |
| Qt models | PASS | QAbstractItemModelTester covers chronological, shared-channel and RT append/eviction/filter/reset/index/selection behavior |
| Four fuzz targets | PASS | 301 seconds each; recording 404,408; index 7,380; protocol 706,792; packet 5,803,699 executions; no finding |
| Available vendor providers | PASS | Compiled from actual licensed headers, contract fixtures and runtime inspection without card initialization |
| SDK 3.6.15 vendor verification | BLOCKED | Licensed SDK/header/runtime package not supplied |
| BU-69065 Menu 4.5.0 | BLOCKED | Separate x86 TestSim32 runtime; matching x64 SDK not supplied |
| Responding RT / both buses / physical four-stream | NOT RUN | No physical card or generator available |
| Physical reconnect / one-hour / 24-hour | NOT RUN | Use portable bench procedure and preserve signed device/reference evidence |

## Four-stream acceptance

Local evidence: out/qualification-final-20261103/receipt.json and its soak/contention performance.json and launch.json files.

| Metric | Ten-minute run | Document contention |
| --- | --- | --- |
| Capture duration | 599.999 s | 30.052 s |
| Generated = recorded = CSV rows = indexed records | 30,026,073 | 1,512,009 |
| Effective total rate | 50,043.54/s | 50,313.09/s |
| GUI heartbeat p95 / p99 / maximum | 90 / 195 / 984 ms | 17 / 24 / 258 ms |
| Raw CRC, payloads, per-stream sequence/counts | PASS | PASS |
| All CSV content / full index scan / 4,096 seeks | PASS | PASS |
| Capture queue high-water / trace cap | 8,192 / 1,024 | 8,192 / 1,024 |
| Actual plots | 4, exercised | 4, exercised |
| Private-memory growth after 60 s | 1,290,240 bytes; below 128 MiB | NOT RUN; scenario shorter than warm-up |
| Display skips | 1,254,360 | 16,195 |
| Injected-source loss / overflow evidence | 0 / 0 | 0 / 0 |
| New document worker observed during capture and matching PDF import completed | Not requested | PASS |

The soak stream totals were CH1-A 7,506,519; CH1-B, CH2-A and CH2-B 7,506,518 each. All were reconciled exactly. Display skips are bounded GUI thinning; raw records were retained and compared. Simulated loss counters are not physical hardware evidence.

The p95 gate is below 100 ms; it is not a zero-lag guarantee. Host ingress-to-viewport-Paint is a separate measurement from GUI heartbeat and physical wire latency. Its soak p95 histogram upper bound was 268.44 ms, maximum 852.51 ms, for applied External RT/raw rows; plot-only intervals are excluded. Other stage quantiles are fixed-memory log2 bucket upper bounds. Direct software traffic does not exercise SDK polling or vendor packet processing, so those stage samples are zero; measure them on the physical bench.

The memory gate covers live capture after warm-up. Offline replay builds a per-record index and retains O(record count) index memory; the live memory gate does not cover that later allocation. End-to-end soak verification, including full CSV and replay checks, took approximately 1,384 seconds.

## Measured fixes and retained failures

The previous full run in out/preflight-final2-20261103 failed the GUI gate at p95 131 ms despite exact raw/CSV/index reconciliation. A short stress check under host pressure also missed its rate gate. Reapplying identical widget styles caused repeated repolish work; styles now change only when their value changes. Scanner offsets no longer call tellg for every record, and bounded CSV formatting avoids per-row string-stream allocation. Final source qualification passed without increasing queue bounds or discarding raw traffic.

An earlier document-contention assertion compared a full-session capture count with an engineering-revision counter that resets when a new ICD is applied. The corrected harness reconciles the full-session RT capture revision and all raw/export/index records, while preserving the revision-scoped counter separately. Prior failed reports are retained locally and are not recast as passes.

## Physical qualification and SDK extension

Use [the bench checklist](SES-Bench-Checklist.md), [local verification](local-verification.md) and [provider contract](provider-contract.md). Establish known responding RT traffic on CH1-A, CH1-B, CH2-A and CH2-B concurrently. Measure actual polling, packet processing, disk pressure, sequence reconciliation, loss/overflow, heartbeat and ingress-to-display independently. Exercise 20 recording cycles and 10 disconnect/reconnect cycles, then one-hour and 24-hour stages. Fixtures never set physical qualification to PASS.

There is no safe claim of compatibility with every past or future SDK. Additional PCs may use other SDK versions, but each distinct header/runtime combination needs a compiled provider and appropriate vendor/card/driver evidence. Unknown fingerprints receive an actionable provider-required diagnostic. The supplied legacy Menu package is a different 32-bit API, not a version that can be loaded into this x64 application.
