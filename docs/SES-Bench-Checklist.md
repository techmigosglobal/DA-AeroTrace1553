# Hardware qualification — AeroTrace 0.5.5 and later development builds

**Highest-priority release gate.** Use an isolated authorized bench with known BC traffic and responding RT equipment. AeroTrace stays passive; configure source/RTs with separate approved equipment. Record wiring, coupling, termination and logical assignments. Do not interrupt an operational bus.

The supplied November 2 A-path loopback is a starting point, not qualification of responding RTs, B paths, four simultaneous streams, endurance or recovery. All cases below start NOT RUN.

## Evidence setup

Record build/hash; machine/OS; Qt/compiler; selected runtime path/hash/API; compile-time SDK; card SKU/serial/firmware; driver INF/service/version; logical mappings; reference expected counts/payloads per stream. Keep a wiring diagram and any kernel-crash evidence. Run Rescan DDC and collect baseline diagnostics. Verify present/started hardware independently of installed packages. No other application may own these channels.

## Required matrix

| ID | Case | Acceptance/evidence |
|---|---|---|
| HW-01 | Responding RT on CH1 A, then CH2 A | Known BC→RT and RT→BC payloads; expected status presence/address; matching raw/replay/CSV; no unexplained loss |
| HW-02 | Responding RT on CH1 B, then CH2 B | Same checks on B; actual B records, not only enabled controls |
| HW-03 | A+B on each channel | Distinct known sequences per bus; independent totals and correct identity |
| HW-04 | Both channels/all four streams | Simultaneous known traffic; same RT addresses on different channels remain separate; compare exact reference totals and payloads |
| HW-05 | Protocol matrix | RT→RT, mode commands, broadcast, missing responses, invalid word count; all views/reports agree on engineering eligibility |
| HW-06 | Sustained/burst traffic | One-hour representative peak, then 24-hour four-stream run or actual operational duration if longer; reference counts, memory, disk latency, queue/GUI latency percentiles |
| HW-07 | Stop/start cycles | At least 20 cycles on retained connection; unique files, correct tail drain and no counter leakage |
| HW-08 | USB loss/reconnect | During separate capture unplug device; visible error, incomplete recording; reconnect/rescan/explicit connect/start; repeat 10 times; no auto-record or simulation fallback |
| HW-09 | Contention/storage | Slow/full disk, GUI stress and concurrent OCR; display skips distinguished from raw loss; explicit incomplete state |
| HW-10 | Shutdown/recovery | Stop near burst; application exit/restart/replay; truncated-tail recovery marked incomplete; no silent overwrite |
| HW-11 | Time | Reference time resolution, rollover/reset and idle gaps; qualify alignment before cross-channel latency claims |
| HW-12 | SDK/driver families | Repeat relevant matrix for each supported runtime/driver/card/firmware combination |

Utilization, response latency and electrical transmit inhibit need appropriate measurement evidence. Status-less generator traffic cannot pass responding-RT acceptance.

## Passive evidence collector

Build the root CMake target aerotrace_qualification. It never transmits, changes drivers/assignments or configures a generator. Use a **new evidence directory with an existing parent**:

~~~powershell
& '<build>/aerotrace_qualification.exe' --output 'D:/Bench/HW04-run001' --seconds 3600 --cycles 1 --streams 1A,1B,2A,2B --logical1 0 --logical2 1 --runtime 'C:/DDC/<selected-sdk>/Lib/x64/Release/emacepl.dll'
~~~

Replace paths/IDs with the bench configuration. --help lists options. Maximum duration is seven days per cycle. Connection is retained between recording cycles; physical unplug/reconnect remains a manual case. Do not run alongside another channel owner.

Outputs: runtime.json, connection.jsonl, runs.jsonl, ATD and capture sidecars. Reports include independent stream counts, valid responses, invalid records and backward times. Exit 0 requires data and at least one valid responding-status message on every requested stream, without reported loss/overflow/read failure. Exit 4 indicates capture/coverage failure; exit 2 indicates arguments/setup/I/O failure. The verdict capture_evidence_complete is **not full hardware qualification**. Compare exact source counts, payloads, reference timing and recovery, then sign the matrix. Interrupted runs may lack a final runs.jsonl row; preserve capture journals/status and raw data.

CTest runs this collector only against a test-only SDK DLL and labels output sdk-fixture. Software fixtures never establish physical reception.

## Sign-off

Record PASS / FAIL / NOT RUN, operator/date, duration/source, expected/observed counts per stream, missing/extra/invalid messages, CSV/replay comparison, timing verdict, diagnostics paths and raw hashes. Support ZIPs omit full recordings/packet sidecars; preserve those separately. Driver crashes remain open until cause and corrective evidence are established.
