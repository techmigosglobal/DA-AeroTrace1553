# AeroTrace 0.6.5 review — engineering simulation

Device Setup provides an explicitly selected RT 3, SA 12, receive, 32-word synthetic example. Its PDF is `examples/demo-icd/Engineering-32-Word-Example.pdf`; `scripts/generate-engineering-example.py` regenerates it from the local example Markdown. IEEE 754 MSW-first and 16-bit integer/identity scale assumptions are simulation-only, not a qualified ICD. Words 28–30 expose raw data bits numbered 4 (MSB) through 19 (LSB). Four plot panels can each select a signal and expand/restore.

No physical hardware/driver/clock or real ICD compatibility is established by this review. The documentation extraction editor, SI conversion, full revision-aware replay, and engineering PDF reports remain work in progress. See `docs/engineering-document-work-in-progress.md` and `docs/mil-std-1553b-traceability.md`.
