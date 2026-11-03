# Detailed application logging

AeroTrace uses its existing asynchronous structured JSONL journal as the single application logging facility. Qt messages feed the same journal; a second logging dependency is unnecessary. The Qt-free capture worker keeps its existing session sidecars linked by run ID, so recording/SDK ownership does not depend on GUI logging.

## Recorded information

Application records include schema, UTC, elapsed milliseconds, run ID, PID, calling thread, monotonic sequence, event, severity, detail and structured fields. Qt records additionally include category, source file, function and line when available. Release builds can omit source context; unavailable context stays empty. Non-success SDK connection results are warnings; explicit failure/error events and nonempty error fields are errors. Fatal Qt/C++ failures attempt a flush and Windows minidump.

Current events cover device discovery/configuration/results, capture lifecycle, document jobs/progress/errors/stderr, profile and reviewed actions, plotting, exports/batch jobs and application shutdown. Capture journals contain raw SDK operation/result/detail, selected runtime, expected compiled SDK structure sizes, one-second health, queue/loss/conversion/time summaries and final integrity. Structure-size diagnostics document the build ABI; they do not verify a foreign runtime ABI.

## Locations and limits

- Application: per-user application data `Logs/application-<run>-<segment>.jsonl`. **F1 → Open logs** identifies the actual path. Each run has its own identity; segments rotate around 8 MiB.
- Recording: `<capture>.atd.events.jsonl`, `.checks.jsonl`, `.configuration.json`, `.status.txt`, profile snapshots/timeline and original `.chN.ch10` or `.chN.mt40` sidecars.
- Qualification collector: `runtime.json`, `connection.jsonl`, `runs.jsonl` plus the ordinary capture sidecars. `sdk-fixture` results are software evidence only.

The application queue is bounded to 8,192 entries / 16 MiB. Write/queue failures are observable through diagnostics and are not silently reported as complete logging. This is operational logging, not a per-message copy of raw traffic; raw data stays in the recorder. A stream flush is not a power-loss durability guarantee. Fatal/crash collection is best effort and cannot capture kernel-driver internals or every abrupt termination.

Use **Collect Diagnostics.cmd** or F1 to gather a local support bundle; nothing is automatically uploaded. Preserve the raw session separately. Logs may contain local paths, hardware identity and ICD excerpts; review a bundle before sharing it outside the bench team. Driver crashes require Windows kernel dump analysis in addition to application logs.

## Verification

`structured_logging_and_crash_dump` checks concurrent events, JSON escaping, sequence/run correlation, rotation, unwritable storage, Qt category/function/line/severity and an isolated Windows crash dump. Existing capture and connection tests exercise SDK results and final integrity. Hardware qualification still needs real driver/card evidence.
