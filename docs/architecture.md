# Architecture — current behavior, 2 November 2026

C++20, Qt Widgets, Designer, Qwt and CMake remain the application stack. The desktop target is `aerotrace_desktop`.

## Ownership

- `aerotrace_core`: Qt-free conversion and record contracts; buffered CRC-protected ATD2 writing and ATD1/2 reading.
- `aerotrace_acquisition`: one `std::jthread` owns source start/poll/stop, DDC packet preservation, recording, address-routed conversion and publication. Hardware uses legacy MT or MTI receive APIs according to reported capabilities. Simulator and injected test sources implement the same interface.
- `aerotrace_application`: Qt-free active profile, bounded analysis state, replay clock/seek, series and export generation.
- Desktop: widget ownership, navigation, settings, strict Qt JSON adapters, file selection, presentation models and Qwt updates. All 18 bodies use Designer forms; controllers bind them rather than construct their static layouts.
- Document child: PDF text/table extraction, OCR, embedding generation, SQLite indexes and cited retrieval. No capture/SDK state crosses this boundary.

`Acquisition::beginConfigured` returns a future immediately. The GUI polls its readiness on the existing refresh timer rather than waiting for vendor initialization. The same single capture thread configures every selected device, reports per-device state through a mutex-protected metrics snapshot, then starts the producers only if configuration succeeds for all selected devices. Cancellation uses its stop token. The synchronous API remains available for CLI/contracts.

The operator shell starts on the monitor. Connect devices explicitly configures selected channels without receiving or recording. Start recording explicitly starts acquisition. Launch, device arrival and configuration changes do not automatically record. Stop & save retains a healthy connection; Disconnect releases it. No SDK initialization sweep is performed. CSV work runs as one bounded job; SDK ownership remains separate from the GUI.

Plot history follows up to eight active signal subscriptions from anywhere in the validated profile (maximum 512 mappings). The capture owner keeps up to 8,192 samples per selected signal and stream and publishes downsampled immutable snapshots at most every 50 ms. Latest values remain available for every mapping. Subscription changes retain overlapping histories and seed new ones only from an actual last matched value. A ninth/later mapping does not depend on the GUI's retained raw-record window. Raw-column plots use the bounded retained analysis data. Cursor inspection snaps to real displayed samples and reports raw-record eviction instead of associating an old point with newer bytes.

## RT monitor aggregation

The approved native side-by-side channel panes use a Designer-backed tree view and a Qt model with stable RT parent items. The capture owner aggregates latest values and counts independently for each channel/RT/bus before display queue thinning. Bounded route children distinguish subaddresses or mode codes, direction, bus and message class; each RT retains at most 128 routes. Immutable snapshots publish with the bounded live cadence. Profile revisions do not reset RT history. A display-only pause freezes tree data while recording continues; chronological raw tables remain available. Raw originals are retained on disk.

## Backpressure and shutdown

Raw records and original hardware packets are written before display publication. There is one bounded 8192-record display queue; saturation drops the oldest display copies and increments a separate counter. There is no unbounded per-record Qt signal queue.

The worker writes in source batches, flushes every 100 ms, checks disk space every second and closes on disk/SDK errors. SDK-reported loss/overflow is an incomplete session, never a healthy status. Stop first stops MTI, drains the bounded SDK buffer pool, writes the tail, flushes and frees SDK state.

A single I/O worker makes ownership simple but cannot hide an arbitrarily long disk stall. The configured SDK pool is bounded (32 × 64 KiB/channel). Hardware load/latency/loss must be qualified on the actual device and storage.

## Conversion and rendering

A compiled address table routes (RT, direction, SA) to at most 512 validated signal definitions. Channel/bus and payload checks remain part of decoding. Histories cap at 8192 valid samples/signal; snapshots publish at 50 ms intervals with at most eight traces and 1024 points/trace. Min/max envelope downsampling preserves endpoints and narrow extrema. Raw disk records are never downsampled.

The GUI consumes record batches on its refresh timer, caps its raw model at 5000 records, and replots bounded samples. Zoom/pan disables axis following until reset. The in-memory analysis archive trims from 200000 to 190000 records.

The capture worker writes an initial `.profile.json` snapshot and an `.icd.jsonl` audit timeline when a revision first processes a record batch. Boundaries use raw record IDs. Replay selects the latest snapshot; it does not automatically reconstruct every historical revision.

## Documents and mappings

A versioned JSON-lines protocol carries one bounded request and progress/result messages. One dialog owns one worker process; cancellation, a timeout and Windows job limits bound its lifetime and resource use. Imported documents are content-addressed by SHA-256.

Embedded tables are preferred over OCR. Ruled scanned tables are segmented into cells so isolated zeros and addresses are not silently omitted. Ambiguous rows retain source evidence and require review. Schema validation runs in both the document worker and C++ before activation. A model answer never modifies the active ICD or hardware configuration.

Retrieval combines BM25 and local 384-dimensional MiniLM vectors. Default answers quote retrieved evidence with page citations. Optional generation calls only a configured loopback endpoint; redirects are disabled. No generative model is assumed to exist.

## Extension and release boundaries

DDC headers/libraries stay in the installed SDK; only the local runtime is packaged. The source contains no BC/RT transmit entry points. This code property is useful evidence, not proof of electrical transmit inhibit.

Cryptographic license-key enforcement, cross-device time calibration, full transaction reconstruction, persistent sparse large-session indexing, expanded sustained hardware stress, RHEL drivers and signed release packaging require their own acceptance evidence.
