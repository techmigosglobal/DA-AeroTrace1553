# DDC SDK and packaged-driver findings — 4 November 2026

Scope: read-only inspection of installed `C:/DDC/aceXtremeSDKv3.6.15`, supplied
`DDC_4.0.3.1/BU-69092S0_3_6_15`, and ZIP members in
`RTPRO_4.1.1_28102026/DDC SDK/`. No installer, vendor sample, driver update,
firmware update, card open, acquisition, or transmission was executed. This
report contains original analysis and source locations, not redistributed
vendor implementation code. The SDK samples carry vendor licensing notices.

## Package identity and available development inputs

| Item | Observed identity | Evidence |
| --- | --- | --- |
| Supplied folder named DDC_4.0.3.1 | Its BU package readme says package 3.6.15.1, 20-Mar-2018; release notes label SDK 3.6.15, 30-Mar-2018. The outer folder is not SDK version evidence. | `DDC_4.0.3.1/BU-69092S0_3_6_15/Readme.txt:7`, `ReleaseNotes.txt:6` |
| Installed SDK 3.6.15 | Full Include directory, C samples, Win32/x64 Debug/Release `emacepl.dll`, dynamic import library and static library. | `C:/DDC/aceXtremeSDKv3.6.15/{Include,Samples,Lib}` |
| Supplied BU-69092S0_4_9_5.zip | 151 members; setup `BU-69092S0_4.9.5.1_setup.exe`, readme, release notes, driver installation trees; no uncompressed SDK headers or sample source in the ZIP member list. Actual release 4.9.5.1, 15-Feb-2023. | ZIP `BU-69092S0_4_9_5/release-notes.txt:12`, `readme.txt:7` |
| Supplied DD-42992S0_4_9_5.zip | ARINC429 Multi-IO SDK, not the 1553 SDK. 95 members. Readme says4.9.5.1,15-Feb-2023; release-notes heading says15-Feb-2022, an internal document date inconsistency. | ZIP `DD-42992S0_4_9_5/readme.txt:2-20`, `release-notes.txt:12` |

The installed 3.6.15 development inputs now permit a future attempt to build an
SDK-matched AeroTrace provider. They do **not** establish that such a provider
has already been built, that its ABI matches another SDK, or that it passes on
the deployed board. A 4.9.5 provider/header comparison remains unperformed:
the supplied 4.9.5 setup was not executed/extracted for SDK payloads in this scope.

## Meaning of channels, logical IDs, and buses

**Observed declarations:** Card Manager associates a logical device number
with an installed DDC device (`Readme.txt:109-113` in the installed Documentation
directory). The SDK has `MAX_NUM_OF_DEVICES=32` (`Include/stdemace.h:60`), a
per-device `wChannel` field for multichannel cards (`Include/structs.h:593-599`),
and an explicit 1553 channel-count field `CHANCOUNT_t.b1553`
(`Include/ddccommon.h:1129-1147`). Do not assume Windows device order equals
AeroTrace External/Internal role order.

The vendor monitor sample separately requests an LDN0–31, a Chapter10 data
channel ID, and a read timeout. These are distinct settings
(`Samples/mti2disk/mti2disk.c:259-288`). Chapter10 channel ID labels stored
packets; it is not a request for another physical bus.

The MTI options include `ACE_MTI_OPT_DISABLE_BUS_B=0x00020000` and
`ACE_MTI_OPT_DISABLE_BUS_A=0x00040000` (`Include/mtop.h:56-57`). Message block
status has `ACE_MT_BSW_CHNL=0x2000` (`Include/mtop.h:42`), and decoded
`MSGSTRUCT` retains the status word, both command/status words and validity
flags (`Include/msgop.h:68-92`).

**Supported interpretation:** monitor each selected logical 1553 channel once,
retain each message's A/B status, and classify messages into CH1A/CH1B/CH2A/CH2B.
Both buses being enabled is not evidence that the host needs one SDK open or
one thread per bus. This is a static contract/model observation; simultaneous
physical capture capacity on the actual board remains a bench question.

## Threading and host receive model

The shipped SDK demonstrates worker-thread acquisition. In
`Samples/mti2disk/mti2disk.c:358-364`, Windows `CreateThread` starts a Chapter10
reader; the main loop remains responsive to stop input. The MTIe sample has a
reader thread and an independent throughput reporting thread
(`Samples/mtiedemo/mtiedemo.c:438-552`, `690-711`). These are samples for one LDN,
not demonstrated per-channel parallel reads across both LDNs.

Internal Windows SDK structures declare dispatcher/worker thread handles and
mutexes: `Include/ddccommon.h:125-156`, `Include/deviceop.h:18-20,724-745`, and
`Include/devicex.h:51-66`. These declarations establish that the library is
designed around interrupt threads and synchronized structures. They do **not**
establish every API is reentrant, that arbitrary simultaneous calls on one LDN
are safe, or that callers may stop/free while another thread reads.

No explicit generic thread-safety, per-LDN reentrancy, or required thread-affinity
contract was found in the installed headers, Documentation readme/release notes,
or focused monitor samples inspected here. Thus changing AeroTrace to independent
SDK reader threads needs further exact-version documentation or controlled
verification; host thread count alone is not a correctness/performance argument.

The vendor sample defines read timeout `-1=wait forever`, `0=do not wait`, and
positive values in milliseconds (`Samples/mti2disk/mti2disk.c:285-288`). A single
host worker can therefore service multiple LDNs with timeout 0 without waiting
for an idle channel, provided drains are bounded/fair and service capacity is
sufficient. That is an architectural inference, not a new measurement.

## MT versus MTI and loss accounting

The MTI sample checks `HWVER_CAPABILITY_MTI` before choosing `ACE_MODE_MTI`
(`Samples/mti2disk/mti2disk.c:265-294`); the capability mask is 0x20
(`Include/ddccommon.h:720`). The available function names alone are not proof
that a particular card supports MTI. MTI/MTIe/MTIr are separate capability bits.

Legacy MT has raw stack polling (`Samples/mtpoll/mtpoll.c:26-31`) and an
interrupt-fed host-buffer path. The host-buffer example installs a buffer and
reads raw records with separate stack-loss and host-buffer-loss outputs
(`Samples/mtirq/mtirq.c:190-213`). Its introduction explicitly contrasts loss
with simple stack polling (`mtirq.c:26-31`). Buffering can reduce loss; no
zero-loss guarantee follows.

MTI exposes device-buffer size, host pool count/block size, zero-copy selection,
packet word/message/time thresholds and interrupt conditions in
`Include/mti.h:101-104`. The installed 3.6.15 headers define pool count 4–32,
block size 4096–524288 bytes (`Include/mtiop.h:81-95`; AceXtreme-specific values
in `Include/mtix.h:35-49`). Do not blindly copy numeric configuration between
device families: general and AceXtreme-specific minimum message/time definitions
differ, and comments themselves have potential unit inconsistencies.

`Samples/mti2disk/mti2disk.c:317-328` configures a finite pool, caller-supplied
buffer, 40960-byte blocks, word/message/time events. That sample documents the
time threshold 1000 as 100 ms (100us units). Events may generate packets based on
time, word count, or message count (`Include/mtiop.h:50-53`); kernel aggregation
and host-read polling are separate stages.

MTI metrics preserve stack fullness/high-water, overflow count, dropped-message
count (`Include/configx.h:9-15`), and `MTI_STATUS` declares receive packets,
pool-empty, dropped-packet, DMA-failure and overflow counters
(`Include/mti.h:78-95`). Timeouts, no data, unavailable buffer and overflow are
different statuses (`Samples/mtiedemo/mtiedemo.c:462-518`). Report them separately.

The Windows sample uses caller-owned packet memory (`fZeroCopyEnable=FALSE`)
and describes `aceMTIFreeCh10DataPkt` as a Windows no-op
(`Samples/mtiedemo/mtiedemo.c:656-680,520-521`). This supports interpreting a
not-supported release as benign only when exact ownership is independently
established. It does not make every release failure benign for every version
or platform.

## Timing, decoding, ABI, and SDK selection

`Include/configop.h:92` maps public `ACE_TT_2US` to 0; `:102` maps 100 ns to 10.
The internal AceXtreme mappings differ (`Include/configx.h:44-55`): internal
2us maps 5 and 100 ns maps 8. Use public SDK constants/APIs at the appropriate
boundary; never substitute internal numeric values. `Include/mtiop.h:151-165`
defines TMATS TTRES metadata, while Chapter10 packets carry 48-bit relative
time (`mtiop.h:200-217`). Packet format by itself is not proof of tick units or
cross-channel clock synchronization.

The Windows public ABI uses `WINAPI` and `extern "C"`
(`Include/stdemace.h:29-55`), Windows U32 is `unsigned long` (32 bits on Windows)
and U64 is `ULONG64` (`Include/ddccommon.h:105-114`), and Chapter10 structures
are packed (`Include/mtiop.h:168,288`). Header size/offset changes and calling
conventions matter as much as exports. Reuse exact SDK headers for a provider,
retain runtime hash/architecture identity, and qualify its driver/firmware/card
combination. Do not infer ABI interchangeability from a shared `emacepl.dll`
filename.

## Packaged drivers and version support

The supplied 3.6.15 x64 Win10 INF set identifies all five driver families as
3.6.15.1 dated 29-Mar-2018. They cover `acexpci`, `acexusb`, `e2mausb`, `emapci`,
and `emaebrpci` (see corresponding INF `DriverVer` lines). `acexusb.inf:34-39`
declares DDC Devices class and catalog; `:103-110` declares a kernel-mode
service. This is package declaration evidence, not proof of current driver load.

The supplied 4.9.5.1 x64 Win10 INF set identifies the same families as4.9.5.1
dated 22-Feb-2023 (each `DriverVer` at line26). The newer USB INF additionally
contains VID4DDC/PID0500 (`driver install 64/Win10/acexusb/acexusb.inf:43-49`).
Its catalog/INF names do not authenticate the signature without verification.

The3.6.15 release notes target XP 32 and Vista/7/8/10 32/64, add Win10 Secure
Boot support, list BU-67113U, and document discontinued hardware support after
3.5.3 (`Documentation/ReleaseNotes.txt:2-83`). They make no Windows 11 support
claim in this release header. The4.9.5.1 supplied release notes explicitly
target Windows 10/11 x64, add Windows 11, list BU-67113U, and identify a known
InstallShield custom-directory limitation (`release-notes.txt:4,18,26-29,69,
124-130`). These supported-OS declarations are distinct from actual local
installation and device acceptance.

The public DDC SDK catalog was checked on 4-Nov-2026 and lists 4.10.2 for
Windows 10/11,4.4.2 for Windows8.1-and-below, and3.5.3 for legacy hardware.
It also lists reference manual Rev G, user manual Rev M, and application note 52
Advanced Monitor Usage. This corroborates that SDK choice follows OS/hardware
family, not merely newest-number preference. The exact local deployment remains
the evidence to qualify. Source:
https://www.ddc-web.com/en/databus/milstd1553-1/software-1/bu-69092

The installed Documentation directory has only readme, release notes and
license; complete SDK manual PDFs were not present there. The official page's
download flow requires login. An attempted retrieval of distributor-hosted
DDC-authored user/reference manuals timed out; their text was not treated as
local inspected evidence. Historical SDK release notes include older resolved
issues; do not attribute an old note to the current release without its heading.

## Static extraction boundary and source identities

Existing 7-Zip 26.03 recognized the safely extracted BU setup as an x86 PE
InstallScript launcher, version 4.9.5.1, ISInternalVersion 25.0.613, with a
75,969,064-byte overlay. It did not expose SDK payload members. A bounded byte
scan found an ISSetupStream marker and no standard MSCF cabinet signature.
No existing unshield/cabextract command was found at the inspected MSYS paths.
No installer execution or installation was attempted. The version/layout
comparison therefore remains limited to package metadata, not recovered 4.9.5
headers. Hashes for original packages, relevant installed headers and both
installed SDK runtime architectures are in `sdk-source-identities.json`.

BU ZIP SHA-256: `4882f42997b1dca6f67e499e006ebe892c701578293361dd655f2e16a209b920`.
BU setup SHA-256: `22fafdbeeb469d07e2671c2f7c363c4794407b005edc5fe6e728da5d347a4809`.

## Answers and unresolved questions

| Question | Status |
| --- | --- |
| What package versions are actually supplied? | Answered for visible3.6.15 and4.9.5.1 metadata; outer folder names can differ. |
| Is the installed 3.6.15 development SDK usable as future build input? | Headers, samples and x64 libraries found. Compile/contract/bench qualification not performed. |
| How can both A/B paths be represented? | Header options and message status support both buses in each logical-channel monitor stream; physical simultaneous performance untested. |
| Are multithreaded host applications supported in principle? | Vendor samples use Windows reader/reporting threads; complete per-LDN concurrency contract remains unresolved. |
| Can SDK 3.6.15 and4.9.5 binaries be swapped transparently? | Not established. Exact4.9.5 header/layout comparison and runtime acceptance remain unresolved. |
| What evidence is needed before adopting parallel LDN readers? | Exact-version thread-safety/lifetime contract or controlled independent-channel stress/loss/stop-drain tests; no change made here. |
| Are ARINC429 SDK inputs needed for current 1553 monitor? | Separate protocol SDK; no direct need established for current 1553-only acquisition. |

No AeroTrace application change or implementation plan was prepared in this
thread; the parent investigation will combine these findings with installed
RTPro binary analysis and AeroTrace source observations.


---
Preserved research appendix. References to adjacent raw JSON/logs in this original
thread report resolve under repository-root `out/investigations/ddc-rtpro-20261104/`.
Selected identity manifests are also preserved in this directory. The consolidated
report one directory above records the later installer-payload hash verification
and manual legacy native call tracing.
