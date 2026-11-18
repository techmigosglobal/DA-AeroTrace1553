# MIL-STD-1553B monitor traceability — development review

Source reviewed: local `MIL-STD-1553B.pdf`, a review and rationale of 1553A/B that reproduces text and includes later notices; page numbers below refer to PDF pages, not printed handbook pages. Applicability and governing baseline must be confirmed for each program. This is not certification.

| Topic | Document | Implemented evidence / boundary |
|---|---|---|
| Passive monitor | section 4.4.4, PDF p. 27 | DDC MT/MTI passive capture; physical transmit inhibition and qualified card combinations NOT RUN. |
| Word fields, parity | sections 4.3.3.4–4.3.3.5.1.6, PDF pp. 5–7 | Command/status/data interpretation and count zero = 32 in `modules/core`; SDK error flags do not expose separate parity/Manchester detail. |
| Mode commands | section 4.3.3.5.1.7, PDF pp. 7–11 | Defined command forms and both mode subaddresses interpreted; optional terminal capabilities cannot be proven from passive silence. |
| Broadcast and RT-to-RT | sections 3.18 and 4.3.3.6, PDF pp. 4, 19–20 | Command/status routing checked; broadcast acceptance cannot be inferred from lack of a response. |
| Message and terminal status | section 4.3.3.5.3, PDF pp. 15–18 | Status fields retained; structural validity separated from busy/fault and conservative payload eligibility. This is not terminal conformance testing. |
| Intermessage gap and RT response | sections 4.3.3.7–4.3.3.8, PDF p. 21 | NOT MEASURED: message timestamps encode 2 us ticks and do not establish word-edge measurement, reference point or accuracy. |
| Service-specific provisions | notice II appendix section 30, PDF pp. 59–61 | Not automatically applied; program applicability and required profile are unresolved. |
| Electrical requirements | section 4.5, PDF pp. 27 onward | NOT MEASURED by this application; use qualified instruments and procedures. |

SDK fixture tests are software checks; all combinations require separate bench qualification. Sequence observations under development do not prove earlier bus history and must be invalidated on discontinuity, loss or unknown ordering.
