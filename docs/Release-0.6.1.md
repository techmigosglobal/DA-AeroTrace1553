# AeroTrace 0.6.1 — verification and delivery record

This is the development acceptance record for the dynamic DDC/provider discovery,
Card Manager recovery, RT statistics and offline prerequisite implementation.
Software checks, packaged software, vendor redistribution rights and physical
qualification are separate gates. An installer candidate is not hardware
acceptance evidence.

## Intended operation

Windows 10/11 x64, C++20, Qt Widgets/Designer and Qwt remain the supported stack.
SDK discovery recognizes installed version folders dynamically and reports the
runtime identity separately from a matching provider and actual channel access.
An unknown SDK is visible rather than being reported as absent. An explicit SDK
selection stays authoritative; clearing it selects automatic discovery.

Provider packs are extensible without recompiling the desktop. Each exact runtime
and provider is checked against its manifest. Automatic selection requires a
matching qualification receipt; manual inspection/bench qualification does not
turn an unqualified combination into production evidence. New provider code is
trusted local native code, not a sandbox.

Card Manager is located in registered/default vendor paths, including
`C:/DDC/ddccm.exe`, or an explicit operator-selected path. Opening it requires
disconnected hardware; elevation cancellation is distinct from missing software.
Discovery alone never opens a card or starts recording.

## RT statistics

The default monitor view has active RT/Bus rows and the reference's ten columns.
Message routes and chronological raw records remain available. Counts are
computed before display queue thinning. Session totals count unique records;
RT-to-RT traffic contributes participation to both relevant RT rows. RT0 is a
valid address; absent command identity is Unclassified; broadcasts do not imply
a missing status response.

No Response, Illegal Mode and Wrong TADR use typed, evidence-backed diagnostics.
Ambiguous data/status-word, gap and BIT categories display an em dash rather
than an invented zero. Filters and display pause do not restrict recording.
Replay statistics are reconstructed from the full recording. ATD2 data and the
existing 42-column paired CSV contract remain unchanged.

## Installer boundary

SDK runtimes belong in private provider-pack directories, not System32. Operator
laptops do not need SDK headers or a compiler. The intended default families are
3.6.15, 4.9.5 and 4.10.2; inclusion requires actual build/intake evidence.

Working drivers and staged packages are preserved. Driver installation/repair is
an explicit, separately logged prerequisite action using unmodified signed
packages. No forced replacement or automatic reboot is permitted. Uninstall
retains shared drivers, Card Manager and user recordings. Driver recovery is not
an atomic application rollback.

## Physical and distribution gates

BU-67113U CH1A/CH1B/CH2A/CH2B qualification remains **NOT RUN** until measured on
attached target hardware with known traffic. Required runs are ten-minute exact
reconciliation and sixty-minute sustained capture, including stop/drain and
failure handling. Each advertised OS/driver/runtime combination requires its own
receipt. Software simulation does not satisfy these gates.

No DDC board was present in the development inventory; Sentinel HL presence is
license-device evidence only. The user supplied a screenshot from an SES machine;
it is not a live inventory of the development laptop.

Vendor payload release also requires the applicable DDC/OEM redistribution terms.
The supplied workstation license text does not itself establish those rights.
The implementation must report missing receipts rather than fabricate approval.

## Executed verification

The Windows x64 desktop build completed with Qt 6.12.0 and MinGW. All 33 CTest
checks passed across the initial run and targeted reruns after corrections. The
packaged RT monitor workflow also passed. Provider compilation, fixture contract
and runtime inspection passed for 3.6.15.1, 4.9.5.1 and 4.10.2.1; those results
do not establish physical capture compatibility. The review deployment is in
`dist/AeroTrace1553-0.6.1-Review/`. Its direct-import dependency audit found no
missing imports. No 0.6.1 installer has been built: the installer compiler is not
available and its installation was rejected by the environment's automatic
approval review. Raw local reports are retained under `out/`.
