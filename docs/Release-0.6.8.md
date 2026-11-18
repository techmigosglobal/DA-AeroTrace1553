# AeroTrace 1553 0.6.8 bench candidate

Device Setup has one Offline simulation source alongside DDC hardware.
Offline simulation generates RT 03 and RT 08, SA 12 receive, 32 data
words per message. The rate is total messages per second across enabled
streams. Old simulator selections migrate without starting capture.

Import **Offline-Simulation-ICD.pdf** from the installed examples folder.
Review and publish its explicit mappings. Start simulation and select an
enabled stream in Engineering or Live Plot. RT 03 starts at 100 km/hr;
RT 08 starts at 490 unitless. The bundled mappings are simulation-only.
For DDC engineering, verify a real ICD mapping against a selected bus
message before publishing. Live values, replay, and reports use the
same route and decode rules.

The installer includes an isolated compact Python runtime for digital
PDF, scanned PDF OCR, and local search. Setup lists the old registered
AeroTrace version and removes it before copying the new files. Stop
and save any capture and exit AeroTrace before upgrading. The installer
keeps the registered location for upgrades and accepts a custom path
for fresh installs. Unattended setup requires `/ACCEPTBENCHNOTICE=0.6.8`.

The November 9 0.6.7 setup log measured about 17 minutes, including
about 15 minutes 41 seconds writing 3,945 Python runtime files. Compare
fresh installation and legacy uninstall separately when benchmarking.

## Physical engineering acceptance

Use the established DDC runtime, pack, receive mode and Card Manager
assignments. With an approved real ICD, verify three independently
known inputs against raw words, Engineering values and Live Plot on the
same stream. Save and replay the capture and compare the same records.
Preserve the software and hardware evidence separately. See the
[bench checklist](SES-Bench-Checklist.md) for monitoring and recovery.

## Software verification and installer

The staged package contains 1993 files versus 4166 in the comparable 0.6.7 staging tree, a 52.2% reduction.
All 36 configured desktop/backend tests and the x64-to-x86 helper fixture passed.
All eight document tests passed against the compact staged runtime. The packaged desktop
also passed PDF review, both-terminal live plotting, replay, and signal reports.
The read-only upgrade check validated the registered 0.6.7 install and rejected a downgrade.

The installer is `dist/installers/AeroTrace1553-0.6.8-Bench-Setup-x64.exe`.
Verified SHA-256 matches the installer sidecar (ECDC14E9100D83EF7EF0C57CCC13C5B804F3B4678B344B2CA50F037A6D5C96B5).

Interactive installation, upgrade behavior and elapsed-time benchmarks remain pending.
The physical DDC engineering acceptance gate remains pending.
