# Local software verification

There are no CI/CD workflows, triggers or deployments in this phase. All commands run on the local Windows computer.

## Toolchains

The desktop/model suite uses the matching Qt Widgets/Test/Concurrent MinGW kit and locally built Qwt. Backend ASan/UBSan and fuzzing use a separate LLVM 22.1.0 x64/MSVC build with static CRT; do not link its C++ libraries into the Qt MinGW executable. `setup-verification-tools.ps1` verifies the pinned official package checksum and GitHub artifact attestation before local extraction. Visual Studio x64 C++ Build Tools, Windows SDK and 7-Zip are prerequisites.

~~~powershell
./scripts/setup-verification-tools.ps1
./scripts/preflight.ps1 -Profile Quick
./scripts/preflight.ps1 -Profile Full
~~~

Quick builds and runs desktop/backend regressions, including Qt model consistency and provider fixtures. Full additionally builds/runs native sanitizer checks, runs each of three libFuzzer targets for at least 300 seconds, then performs a 600-second 50,000-message/s four-stream soak and a 30-second document-processing contention run. Fuzzers run sequentially and finish before baseline GUI timing begins. Build scripts default to one compiler job to keep memory pressure controlled on a 16-GiB PC; build.ps1 and build-verified-backend.ps1 accept -Jobs for larger machines. Explicit shorter durations are development diagnostics, not completion of the full gate.

Use -BuildDirectory, -BackendBuild, -QtRoot and -ReportDirectory for local paths. -SkipBuild checks an already built desktop; the native verification build still configures itself. SDK 3.6.15 is a separate BLOCKED result unless AEROTRACE_SDK_3615_ROOT is supplied. Exit 1 means a failed check, exit 2 means missing mandatory evidence/tooling, and exit 0 means all selected checks passed. Hardware remains NOT RUN regardless of software status.

Receipts include commit identity, profile, per-check status, duration, logs and JUnit evidence. Capture and fuzz artifacts stay under ignored out directories or an explicit report directory. A local report is not uploaded automatically.

## What is verified

- QAbstractItemModelTester exercises chronological/shared-channel/RT models, insertion, bounded eviction, filtering, resets, persistent indexes and invalid indexes. Qt Test is a test-only dependency.
- ASan/UBSan run the portable backend; deliberate child-process canaries must demonstrate real heap-overflow and signed-overflow detection. Vendor DLL internals, Qt and drivers are not instrumented by this build.
- Fuzz targets use production stream parsing, protocol/payload conversion, packet-envelope and decoded-message bounds. A CRC-repaired second parsing pass lets mutations reach record fields. Seed inputs include ATD1/ATD2, invalid lengths and truncated envelopes; deterministic tests also cover changed indexed recordings, CRC failure and timestamp limits.
- LLVM's prebuilt Windows libFuzzer requires matching STL annotation settings. Only fuzz binaries disable string/vector container annotations; ordinary backend sanitizer checks retain them. The sanitizer linker disables identical-code folding (/OPT:NOICF) so globals with ASan redzones are not folded together. Deliberate canaries verify both allocation and signed-overflow detection. This does not disable ASan allocation-boundary checking.
- Crashes/timeouts are FAIL, not successful fuzz completion. Preserve crash inputs and logs, minimize the input, add a regression case and rerun the affected target. Five minutes is a bounded smoke campaign, not proof that no defect exists.

## Stress acceptance and measurement scope

Deterministic input carries independent per-stream sequences, RT 2/3 activity, known words and status evidence. The full workflow compares every generated/recorded payload, every paired CSV row, every record visited while building the replay index, and up to 4,096 distributed indexed seeks. Inter-stream clock calibration is not inferred.

Acceptance: exact counts/payloads, intact recording CRCs, no capture loss/overflow, throughput at least 49,000/s, p95 GUI heartbeat below 100 ms, bounded queue/trace sizes, actual plotting and private-memory growth below 128 MiB after 60 seconds of warm-up. Record p99 and maximum heartbeat as well; a passing percentile does not mean zero lag.

Stage distributions use fixed-memory log2 histograms with inclusive upper-bound quantiles. Separate SDK polling, packet processing, packet/record writes, conversion, RT aggregation, publication, flush, checks, overall worker and GUI update costs. Software ingress-to-Paint samples concern an applied External RT/raw row, exclude the plot-only interval and do not measure physical wire latency. SDK stages are naturally empty in a direct software-source run; the hardware collector emits those stages on the bench.

The 50k software workload is not a proposed physical 1553 bus schedule. Physical rates, traffic mix, timing accuracy, electrical behavior and reconnect/endurance acceptance require the real bench and reference equipment.

## Portable bench kit

~~~powershell
./scripts/package-bench-kit.ps1 -BuildDirectory '<native-desktop-build>' -Destination '<new-kit-directory>'
./run-bench.ps1 -Runtime '<local-SDK-folder-or-DLL>' -OutputDirectory '<new-evidence-directory>' -Seconds 3600 -Logical1 0 -Logical2 1 -ReferenceRecording '<known-reference.atd>'
~~~

The kit includes passive collector/comparison executables, production providers, required MinGW libraries, hashes and procedures. It does not contain licensed vendor packages or generate BC/RT transmissions. Configure the generator/responding RTs separately.

Reference comparison hashes ordered commands, statuses, flags, presence and payload independently for each stream, permitting different inter-stream arrival ordering. Timestamps/host IDs are excluded and timing accuracy remains NOT MEASURED. Keep reference provenance and the entire capture directory. The checklist's responding-RT/B/four-stream, one-hour/24-hour, 20 stop/start and 10 physical reconnect cases remain NOT RUN until executed and signed on the real bench.
