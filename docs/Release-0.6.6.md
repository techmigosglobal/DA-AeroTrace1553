# AeroTrace 0.6.6 bench candidate

Use the 32-bit compatibility helper, automatic MTI/MT receive mode, and the
verified `ddc-3.6.15-x86` pack for the affected BU-67113U laptop. Connect and
start recording explicitly. Healthy empty polls keep the recording active.
Missing heartbeats, stalled operations, and failed final acknowledgement are
capture faults. Fresh connection requires confirmed process exit and no
reported SDK cleanup failure. `aceFree` result 22 remains unresolved.

## Operator workflow

Review signals accepts manual entry, CSV and PDF evidence. Drafts and chat
stay separate from the active profile. Preview a field with raw hex or a
selected record, then publish the complete staged profile. Use the four plot
selectors after publication. The supplied 32-word example requires explicit
demo assumptions and verified example-simulation provenance.

## Software verification

On 8 November 2026, desktop CTest passed 36 of 36 checks and the standalone
document worker passed 7 of 7. Both PDF workflows passed again after the
plot layout update. The matched-window stress run measured 49,990 records/s and a 33 ms p95 GUI heartbeat. Exact record and stream
reconciliation passed. The maximum heartbeat gap was 271 ms. The x86
fixture tested a 12-second quiet start, 12 seconds of silence between
traffic bursts, both watchdogs, and disconnect and cleanup failure paths.
These results use software fixtures.

## Targeted bench acceptance

Physical acceptance remains **NOT RUN**. Use the same installer hash for
every bench step. Record OS, driver, card, logical assignments and runtime
and helper hashes in the diagnostic bundle.

1. On the isolated bench, connect with no traffic. Start recording and wait
   five minutes. Introduce known traffic, pause it for five minutes, then
   resume. The app remains responsive throughout healthy polling.
2. Stop & save and verify final record and packet acknowledgments, ATD CRC,
   and paired CSV counts and hashes against reference traffic. Start and
   stop another run while retaining the connection, then disconnect and
   reconnect explicitly.
3. Separately interrupt a recording by device loss. Preserve the incomplete
   run. Reconnect explicitly after ownership release. Collect SDK cleanup
   results, including any recurrence of result 22.
4. Use a small approved real ICD or CSV with matching traffic to verify
   engineering values and live/replay plots. Exercise keyboard navigation
   through the review grid and plots. Preserve evidence by installer hash.

The later responding-RT, simultaneous four-path, endurance, native x64 and
clock alignment checks remain separate. See the
[hardware qualification checklist](SES-Bench-Checklist.md).
