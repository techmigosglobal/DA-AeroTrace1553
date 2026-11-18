# AeroTrace 1553 v0.6.8 bench candidate codebase memory

Recorded 9 November 2026. The release record is docs/Release-0.6.8.md, and the operator walkthrough is docs/icd-documents.md.

- **Offline simulation.** Device Setup exposes DDC hardware and one Offline simulation mode. Window::restoreCaptureSource migrates EXAMPLE and MAPPED_ICD to SIMULATOR without starting capture or publishing a profile. aero::simulationRecord produces RT 03 and RT 08 on SA 12, receive, with 32 words per message on each enabled stream. The rate is aggregate across streams and defaults to 12 messages/s. The bundled Offline-Simulation-ICD.pdf has 19 explicit mappings and requires explicit publication.
- **ICD provenance and matching.** The packaged PDF is identified by its content hash and remains simulation-only. New capture sidecars carry offline_rt03_rt08_v1. desktop::verifiedOfflineFixture also recognizes verified legacy example sidecars. aero::matchesSignalRoute is shared by payload decoding and decodeValue, so unrelated traffic does not count as matching in reports. Review signals can verify a selected message's route, word order, raw words, scaling, and result before publication. Hardware engineering acceptance requires an approved real ICD and three independently known live values.
- **Setup and runtime.** Inno uses the registered, protected AeroTrace uninstaller before writing a replacement, with graceful desktop exit and user-data checks. The Python runtime uses a pinned embeddable CPython 3.14.7 ZIP, a hash-locked Windows x64 wheel set, and an archive for compatible pure-Python modules. Retain offline PDF, OCR, local retrieval, the x86 helper and all three native DDC provider packs. The staged payload fell from 4,166 to 1,993 files, a 52.2% reduction. This is a file count, with elapsed install time still to be measured.
- **Verification.** All 36 configured tests, eight compact-runtime document tests, and the x64-to-x86 helper fixture passed. The packaged desktop passed PDF review, both-terminal plotting, replay and report checks. The read-only upgrade preflight validated the registered 0.6.7 install and rejected a downgrade. The 0.6.8 bench installer version and SHA-256 sidecar matched. Interactive install and clean replacement, elapsed-time benchmarks, and physical DDC engineering acceptance remain pending.

Use direct source for packaging scripts, installer logic, and the Python worker where the fast graph omits them. Keep local vendor inputs and bench data out of the graph.

Graph snapshot verified with 1,887 nodes and 9,728 edges.
The index found simulationRecord, matchesSignalRoute, verifiedOfflineFixture,
and restoreCaptureSource. The coverage check flagged metadata discrepancies
and some partial C++ parses. Use source to resolve those gaps.
