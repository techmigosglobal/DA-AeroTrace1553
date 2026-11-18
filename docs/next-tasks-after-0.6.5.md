# Next tasks after 0.6.5 — status

Baseline: the 0.6.5 review installer (`dist/installers/AeroTrace1553-0.6.5-Review-Setup-x64.exe`) and its checksum exist. The 0.6.5 source changes are uncommitted, alongside MIL-STD and document-worker changes. Preserve them.

Hardware acceptance is NOT RUN unless noted.

Last full run: `scripts/build.ps1 -Test` → CTest 35/35 passed (`out/build-final-test.log`).

## Topics

1. **DONE.** Quiet-bus false stall. `tools/ddc_helper/main.cpp` advances `lastProgress` after every completed poll (empty included). Bounded detection for real hangs is kept. Timeouts were not raised.
2. **DONE (software).** Helper `close()` treats a dead or terminated helper as transport failure, not SDK cleanup failure. A failed Close request on a live helper still blocks reconnect. `helper_integration_tests.cpp` asserts a clean close after a crash and a fresh reconnect. `sdk_owner_tests.cpp` true-cleanup-failure cases are unchanged. Physical reconnect is hardware acceptance (topic 6).
3. **OPEN — not classified.** `aceFree: Unknown error number (22)`. Error 22 is not in `errordef.h` (ACE_ERR_* uses 0, 1, 50–54 and negative codes). The SDK's own `aceErrorStr(22)` (x86 `emacepl.dll`, SDK 3.6.15) also returns "Unknown error number", so the SDK does not name it. Meaning needs vendor documentation. Not classified in code.
4. **DONE (single-signal path).** A saved ICD draft (`draft:true`) is still rejected by `profileFromJson` by design. `icd_pages.cpp` `importIcdPath` loads a single-signal draft into the ICD editor and shows a toast; it does not activate it. Publishing goes through the existing `readEditor` → `applyProfile` gate. `workflow-test` check "Single-signal ICD draft loads into the editor for review without activating" passes (`out/build-topic4.log`, CTest 35/35). Multi-signal drafts are still not loadable (the toast says to open a single-signal draft). The editor-to-publish step itself is not separately tested. Simulation-only assumptions from `examples/demo-icd/Engineering-32-Word-Simulation-Notes.md` are unchanged.
5. **DONE.** `Unsupported ICD schema` comes from `profileFromJson` (`profile_io.cpp:50`) when a profile is rejected. Startup (`main_window.cpp`) and replay (`session_pages.cpp`) now log the file path and error with `qWarning`. The user-facing toast does not name the file. The schema is unchanged.
6. **NOT RUN.** Hardware acceptance (quiet-bus soak, pause, stop/save, reconnect, PDF activation, engineering values, plots, persistence, replay). Requires the affected laptop and DDC card.
7. **OPEN — partly fixed, gate still fails.** `performance_qualification.cpp` now reads `elapsed` and `total` together, before `stopRecording()`. Build and CTest 35/35 pass (`out/build-topic7.log`). The stress rerun still fails the rate gate: 56,015 msg/s over 3.001 s, 168,101 records. The generator clock starts in `startMonitoring()` (`qualification_source.hpp`), before the measured clock starts, so the numerator covers more time than the denominator. A full fix changes the measurement definition, which needs your approval. The gate and threshold are unchanged.
8. **OPEN.** Uncommitted 0.6.5, MIL-STD and document changes. Commit grouping not decided. Nothing committed or pushed.

## Not in scope now

Native x64 reception, four-stream physical gate, Session SQLite/DuckDB, CI/CD.

---

Remaining open topics:
- 3: error 22 meaning, needs vendor support (not in vendor manual Appendix A).
- 6: hardware acceptance, NOT RUN.
- 7: generator clock vs measured clock mismatch; fix needs your approval.
- 8: commit grouping, needs your decision.
