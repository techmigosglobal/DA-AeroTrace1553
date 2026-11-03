# Development readiness — 3 November 2026

This is an implementation audit of the development source, not a declaration that every proposed technology has been integrated or that physical hardware is qualified.

| Capability | Actual status |
|---|---|
| CH1 A+B and CH2 A+B together | Implemented; SDK fixtures cover all 15 nonempty stream masks and the native RT workflow checks bus/channel separation. Physical four-stream acceptance remains open. |
| Lag-free / overflow-free monitoring | Not established. Bounded display buffering, batched updates and explicit hardware loss/overflow reporting exist. Disk writes, conversion and SDK polling share the capture worker, so slow storage or expensive processing can delay subsequent reads. |
| SQLite | Existing Python document store uses SQLite WAL, FTS5, embeddings and chat history. Session metadata, revision-aware replay, annotations and bookmarks have not been moved to a transactional SQLite catalog. |
| DuckDB / Parquet engineering datasets | Not implemented or benchmarked. |
| Python OCR | Implemented in the isolated document process with local RapidOCR/ONNX models and digital/scanned fixture tests. |
| ONNX representative-ICD acceptance | Inference/retrieval exists; a representative labeled ICD corpus, numerical extraction-accuracy gates and accepted corpus results remain outstanding. |
| Isolated scientific NumPy/SciPy jobs | Not implemented. NumPy in the document worker does not establish a scientific-analysis job service; SciPy is not a current dependency. |
| Local development builds | CMake Release/core presets, build script, CTest and an isolated verified development build exist. |
| Automated CI/pre-merge verification | No configured CI workflow in this source bundle. Local test scripts do not constitute CI. |
| Qt model tester / sanitizers / fuzzing | GUI regressions and model-related assertions exist; QAbstractItemModelTester, sanitizer presets and parser fuzz targets remain outstanding. |
| Detailed logging | Implemented structured JSONL, Qt severity/category context, capture sidecars and local diagnostics collection. |
| Per-PC SDK configuration | Folder/DLL overrides and DDC_SDK_ROOT precedence implemented without a version-number allowlist. Incompatible ABI families still need dedicated providers and hardware acceptance. |

## Performance evidence and limits

The existing synthetic stress report from the development validation recorded 157,682 messages over 2.999 seconds at a requested 50,000 events/s. It reported a 20 ms p95 GUI heartbeat, a 188 ms maximum heartbeat gap, 4,201 display skips and a 21.6645 ms maximum worker batch. Recording CRC passed and no capture error was reported. This short, simulated run is evidence that occasional UI delays/display thinning occur; it is not a full-load DDC endurance result, a capture-to-screen latency measurement, or a cross-PC guarantee. The report is `test-results/performance.json` in the validation build directory.

Release acceptance must compare exact reference counts/payloads for every physical stream while measuring capture/display latency percentiles, queue pressure, SDK stack fullness, memory and disk latency. Run responding RTs, B paths, concurrent OCR, slow storage, stop/start and physical reconnect against the [hardware matrix](SES-Bench-Checklist.md). Any stack overflow or hardware loss makes the run incomplete; reporting it does not prevent it.

## SDK deployment requirement

Each PC may use its own installed SDK/driver combination. The runtime selection order is an application-specific folder/DLL, then DDC_SDK_ROOT, then unambiguous installed SDK discovery, then packaged/build fallback. A wrong explicit configuration fails visibly. [The SDK guide](sdk-compatibility.md) gives operational details and supported directory layouts.

Runtime export discovery cannot establish vendor structure layouts or semantic compatibility. Versioned providers or isolated helpers compiled against each incompatible SDK family are the next compatibility architecture; they are not implemented in the current executable. Exact SDK, runtime hash, driver, card and firmware combinations require acceptance evidence.

## Next work in priority order

1. Physical four-stream qualification and bottleneck measurements before making performance guarantees.
2. Provider contracts and fixtures for the actual SDK families deployed across the target PCs; obtain vendor headers/runtime/driver evidence for each.
3. Reproducible local preflight plus CI configuration, Qt model consistency checks, sanitizer builds and corrupt-input fuzzing.
4. Transactional SQLite session metadata and revision-aware replay.
5. Measured DuckDB/Parquet engineering analysis, followed by isolated NumPy/SciPy jobs with reproducible inputs/results and cancellation.
6. Representative labeled ICD acceptance covering scan quality, layouts, scales, signedness, word order, citations and rejection of unsupported/ambiguous mappings.
