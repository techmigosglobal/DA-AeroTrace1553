# AeroTrace 1553 0.6.7 bench candidate

The offline wizard asks for acceptance before showing the install location.
It reports file installation progress and the outcome of DDC prerequisite
inspection. Unattended setup requires `/ACCEPTBENCHNOTICE=0.6.7`.

Review signals prefills explicit PDF fields and offers a bundled example
preset. Device Setup retains the selected generic, example, mapped ICD,
or DDC source across restarts. Start capture explicitly. Engineering,
plots, and signal reports use decoded packet values. Reports can cover a
retained live snapshot or a full saved recording. Bus Analysis groups
Inspector, Topology, and Alerts. Existing task files remain on disk.

Build and test with `scripts/build-release-067.ps1`. The review bundle
continues to use the provider and helper identity checks and the physical
bench procedure in Release-0.6.6.md. Physical DDC acceptance remains NOT RUN.

## Verification

The desktop suite passed 34 of 35 checks on the full run. The remaining
navigation check passed on rerun after its assertions were updated for
the shared Bus Analysis heading and tabs. The eight standalone document
worker tests passed. The bundled PDF workflow checked all four live plots
and a 10,000-record signal report. Native provider packs and the x86 helper
passed their software fixture checks.

Interactive Windows setup and upgrade checks in an isolated machine and
physical DDC acceptance are pending.

## Installer

Built and checked version 0.6.7 in the executable metadata. The local output is
`dist/installers/AeroTrace1553-0.6.7-Bench-Setup-x64.exe`.
The SHA-256 sidecar matched an independent hash of the installer.
