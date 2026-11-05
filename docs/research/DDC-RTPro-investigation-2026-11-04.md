# DDC, RTPro, and AeroTrace investigation — 4 November 2026

Research stage requested by Vinay. Application implementation and its plan are
deferred until the findings are reviewed. This report distinguishes installed
file/package observations, source declarations, static inference, and runtime
unknowns. No vendor application, installer, SDK sample, hardware probe, or capture
was executed. No driver, registry, SDK installation, firmware, or application
implementation was changed.

## Findings that affect the next discussion

1. The supplied installers contain the **same main executable bytes** as the
   installed legacy RTPro and NxtGen applications. This correspondence was
   verified by SHA-256 after decoding validated embedded 7z members, without
   running either installer.
2. **Two logical channel captures, each carrying A/B provenance**, represent
   CH1A/CH1B/CH2A/CH2B. Four stream labels do not imply four hardware handles or
   four threads. Vendor headers and AeroTrace source implement this distinction.
3. Legacy RTPro has a device-associated `BusMonitorThread` derived from Qt's
   `QThread`; its worker reaches an MTI packet read with **timeout 0**. This is
   substantially stronger evidence than a threading-related import or string.
   The number of concurrently running instances and physical performance were
   not observed.
4. AeroTrace already supports configuring both channels and both buses. One
   recording worker performs SDK polling, disk recording, decode/conversion, and
   aggregation. Slow work in that chain can delay later polls. This is a source
   structure observation, not a reproduced data-loss fault.
5. Installed SDK **3.6.15 inputs now exist**, so the old missing-input blocker
   is historical. No provider was built or qualified from them during this task.
   The supplied 4.9.5 package identifies itself as **4.9.5.1**, but its setup
   payload did not expose headers through the available static extraction tools.
6. DriverStore contains **3.6.15.1 and 4.10.2.1** packages. The read-only inventory
   found no enumerated DDC-class device or matching active service. Staged driver
   packages do not prove an active hardware configuration.
7. NxtGen's protected/opaque native code prevents a reliable static conclusion
   about its capture-thread topology in this environment. No configured REA
   deep native engine was available; no license protection was bypassed.

## Evidence locations and identities

Repository HEAD inspected: `9955af7fc66b5ba3ebb8e9c76c34528a7bf1838c`.
Raw task artifacts are under `out/investigations/ddc-rtpro-20261104/` (local,
ignored build-output area). The structured evidence is supplementary; the
material findings and source locations are preserved in this report.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Supplied RTProAnalyzer_1.0.exe installer | 37,551,522 | `ab9a73873ef9216aefc6499e94b1e0fcc2395b8c19c3d8de1c98442453f82429` |
| Installed / embedded legacy main | 3,344,896 | `2b4a3de447eb1bf83a6e45f4e4257f77c5402327463f2c6eec80df6dd0582da4` |
| Supplied NxtGen installer | 185,011,496 | `106c6bd49d1800bc3464d149c8e0420db7a26f7f61a293a04697b4f32268a8cc` |
| Installed / embedded NxtGen main | 25,092,608 | `c8bec50fc39292fec7f2b678a6d3d7ddccb106372c296c5025540d746cb5b001` |
| Supplied BU-69092S0_4_9_5.zip | See source manifest | `4882f42997b1dca6f67e499e006ebe892c701578293361dd655f2e16a209b920` |

Direct 7-Zip listing did not recognize either complete RTPro installer. A bounded
scan then validated embedded 7z start-header and next-header CRCs and inventoried
6 legacy and 14 NxtGen archives. Main executable members were streamed to SHA-256
without execution. This verifies the selected executable correspondence, not
every installer operation or complete package-to-installation equality. The
NxtGen recovered members contain no emacepl.dll; legacy includes it.

REA was reached through its existing local **rea-agents 6.8.0** stdio server,
using the server's advertised schemas. No MCP configuration was changed.
Complete returned records were exported to `rea-desktop-bundle.json` and
`rea-pe-bundle.json` in the task artifact directory. Native sessions were not
opened; the temporary stdio processes exited after export. The installed skill
is newer than this server; unadvertised operations were not assumed available.

| REA record | Evidence ID |
| --- | --- |
| Legacy desktop inventory, parent | `ev_92d12f1b554d0437952b9274d7f74221419d3b6e4a87cc3ec62825ec5615e795` |
| NxtGen desktop inventory, parent | `ev_71941b723d9d33a8b5dca866212d22292e122f6b15ca1deb2933d6adf7347d69` |
| Legacy PE imports | `ev_81bc0280f0df571db0823e9d53d0aaa3b8d55b935d5e6b140113e6586fc933e3` |
| NxtGen PE imports | `ev_c7c82220491ad72962da9313ba70ee1370adf66485ffc930e733e0feca057911` |
| Legacy bundled SDK exports | `ev_1a01b34e355b5c882e8d6b0c334081a6df10f7b56c1a512784b865719eca77ea` |

Directory inventory examined 63 legacy and 1,263 NxtGen entries with no reported
omissions. Its directory digest covers examined metadata/read ranges, not every
file's bytes. PE import/export records and individually hashed binaries have
their own artifact identity. Shell, registry, SDK source, entropy, and objdump
observations below are labelled separately and do not inherit REA authority.

## What is installed versus what is supplied

| Component | Observed version / architecture | Meaning |
| --- | --- | --- |
| C:/DDC/ddccm.exe | 4.0.3.1, x64 | Card Manager. Explains the outer DDC_4.0.3.1 label; not the C SDK version. |
| C:/DDC/aceXtremeSDKv3.6.15 | SDK 3.6.15; DLL 3.6.15.1, x86 and x64 | Includes headers, samples, import/static libraries, Debug/Release runtimes. |
| C:/RTips Technologies/RTProAnalyzer | Registered 1.0; main x86; Qt 5.13.1 | Bundles x86 emacepl.dll **3.6.13.1** and Qwt. Main has no version resource. |
| C:/RTips Technologies/RTProAnalyzerNxtGen | Main product4.1.1 / file4.1.1.142, x64; Qt6.10.2 | Registry4.1.0, components package4.0, and old readme entries are stale metadata. |
| NxtGen RTProOfflineAnalysis | Separate x64 executable plus Python3.14.2 | Packages PyQt6, pyqtgraph, NumPy, pandas, SQLite. File presence is not a scheduling trace. |
| Supplied BU-69092S0_4_9_5.zip | SDK setup4.9.5.1,15-Feb-2023 | 1553 SDK. Package notes explicitly target Windows10/11 x64. Not found installed as a development SDK. |
| Supplied DD-42992S0_4_9_5.zip | ARINC429 Multi-IO SDK4.9.5.1 | Separate protocol. No direct requirement for AeroTrace's current1553 capture is established. |

The older 3.6.15 release notes cover XP32 and Vista/7/8/10 variants, including
Win10 Secure Boot changes; they do not make a Windows11 support declaration in
that release header. Both inspected SDK releases list BU-67113U. Supported-OS
declarations are not live validation of this PC/card/driver combination.

Installed RTPro readmes describe device-number selection, start/stop monitoring,
and recorded `.db` access (`ReadMe.txt:22,26` at each application root). No actual
recorded database schema, replay fidelity, or power-loss behavior was verified.
NxtGen packages a separate offline analysis process, which is relevant to
keeping scientific work outside acquisition; actual interprocess scheduling was
not observed.

## Driver and DLL state on this PC

PnPUtil identified the following DDC Devices class packages, GUID
`{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}`:

| Family | 3.6.15.1,29-Mar-2018 | 4.10.2.1,16-Sep-2026 |
| --- | --- | --- |
| acexpci | oem71.inf | oem59.inf |
| acexusb | oem70.inf | oem60.inf |
| e2mausb | oem72.inf | oem61.inf |
| emaebrpci | oem73.inf | oem62.inf |
| emapci | oem74.inf | oem63.inf |

These are **staged packages**. DDC-class PnPUtil enumeration reported no devices;
bounded CIM device/signed-driver filters and exact driver-service/file-name
queries found no matching active binding/service. No SDK device-open API was
called. Which driver would bind to newly connected hardware remains unknown.
PnPUtil signer metadata was recorded; independent signature validation was not
performed. The supplied4.9.5 package's drivers are4.9.5.1,22-Feb-2023, a third
package identity distinct from the two observed DriverStore versions.

NxtGen has no installed app-local/nested emacepl.dll. It does declare that DLL
in its PE imports. `C:/Windows/System32/emacepl.dll` is an existing x64 3.6.15.1
loader candidate, SHA-256
`74cfb017fa96dc102184430f97d58553930f5d2348c2602d99022e4dbd60abfe`.
Actual module loading was not observed. The SysWOW64 counterpart is x86.

The x64 SDK release copy has SHA-256
`71033e145faa0e3d0303565cf15b2c274b4428ce91f53f3cf393c3a023511231`.
SDK/system copies have matching size/version and identical bytes in every PE
section. Differences lie in header checksum/overlay bytes. Signing changes are
a plausible explanation, not authenticated fact; whole-file identity is still
different under AeroTrace's current provider contract.

## Channel, bus, and SDK receive semantics

Installed vendor sources referenced below are under
`C:/DDC/aceXtremeSDKv3.6.15/`. These are observed declarations/samples, not
executed tests:

- `Documentation/Readme.txt:119-124`, `Include/stdemace.h:60`, and
  `Samples/mti2disk/mti2disk.c:259-288` distinguish logical device number0–31,
  physical channel assignment, Chapter10 packet channel ID, and read timeout.
- `Include/mtop.h:56-57` exposes disable-bus-B/A option bits; `:42` exposes
  `ACE_MT_BSW_CHNL=0x2000`. `Include/msgop.h:68-92` retains status and validity.
  Thus one configured logical-channel monitor can return A/B-labelled records.
- `Samples/mti2disk/mti2disk.c:265-294` checks MTI hardware capability before
  selecting MTI. Export presence alone is not a card capability check.
- `Samples/mti2disk/mti2disk.c:358-364` creates a Windows reader thread.
  `Samples/mtiedemo/mtiedemo.c:438-552,690-711` separates receive/report work.
  These examples do not establish arbitrary same-LDN reentrancy or safe
  stop/free concurrent with a read.
- `Samples/mti2disk/mti2disk.c:285-288`: timeout0 is nonblocking, positive values
  are milliseconds, -1 waits indefinitely. An idle channel need not block a
  shared reader when timeout0 and bounded/fair draining are used.
- `Include/mti.h:101-104`, `Include/mtiop.h:50-53,81-95`, and the sample's
  `:317-328` describe finite device/host pools and message/word/time events.
  Kernel-side aggregation, SDK reads, decoding, disk writes and display refresh
  are separate sources of latency. Copying a thread count does not size them.
- `Include/configx.h:9-15`, `Include/mti.h:78-95`, and
  `Samples/mtirq/mtirq.c:190-213` expose different overflow/drop/loss counters.
  No-data, timeout, lost packets and SDK errors must remain distinct.
- `Samples/mtiedemo/mtiedemo.c:520-521,656-680` describes Windows caller-owned
  packet memory and packet release as a no-op in that sample. This does not
  excuse arbitrary release failures for other ownership/platform/version cases.
- `Include/stdemace.h:29-55`, `Include/ddccommon.h:105-114`, and
  `Include/mtiop.h:168,288` define calling conventions, fixed Windows widths and
  packed structures. Equal export names do not prove equal ABI/layout.

No complete general cross-LDN thread-safety or required affinity contract was
established by the inspected headers, samples and installed readmes. SDK internal
mutex/thread fields are implementation declarations, not blanket API guarantees.

## What static reverse engineering established about RTPro

### Legacy version

The main x86 application imports 48 functions from emacepl.dll **by ordinal**.
They were resolved against the exact bundled3.6.13.1 DLL's export table, not
guessed from a different SDK. This maps ordinals150–154 to MTI configure/read/
release/start/stop and161 to raw-message decoding. It also imports BC/RT APIs;
RTPro is not solely a passive monitor and its transmit paths are not templates
for AeroTrace.

The following are static assembly/RTTI findings from GNU objdump and byte
mapping, with binary identity pinned above. Addresses are legacy image VAs at
image base0x400000, not source line numbers or runtime observations:

| Trace point | Observed connection |
| --- | --- |
| RTTI string file offset0x2f700c, VA0x6f900c | `16BusMonitorThread`; typeinfo0x6f8990; associated vtable0x6fb554. |
| Constructor0x4a6f70; call0x4a6f7d | Calls imported QThread constructor, then installs that vtable. |
| Device-associated creation0x4985eb, setup0x49869d | Stores the new worker in a selected device entry and supplies a device-number field and owner pointer. |
| Start0x4986e8 | Calls QThread::start through IAT0x723d6c on that stored worker. |
| Worker virtual entry0x4a6fc0; call0x4a6fea | Loads stored device-number field and calls packet-processing function0x46f990. |
| Read0x46f9e4–0x46f9ff | Sets third argument0; calls thunk0x4ca440 → IAT0x7239d4 → ordinal151 → aceMTIGetCh10DataPkt. |
| Configure0x46e331 | Calls ordinal150; visible argument setup includes128KiB device buffer,16 blocks,40960-byte block and zero-copy false, consistent with the inspected SDK signature. Exact3.6.13 header equivalence is not independently qualified. |
| Worker exit branch0x4a6ff2–0x4a7006 | Stop flag switches from ordinary reads into a counter-based final read loop; counter initialized10, with QThread::msleep(10) in that branch. This is not a10ms sleep on every normal read. |

This supports the inference **a QThread worker associated with each configured
monitor/device instance**, receiving packet batches outside the GUI thread.
It does not prove two instances were active concurrently in any observed run,
that busB was receiving, that latency/loss bounds are met, or that this topology
is optimal for AeroTrace. Cross-thread updates, ownership races, full stop/drain
correctness and database behavior were not comprehensively audited.

### NxtGen version

The native main is x64, links Qt6 and declares emacepl.dll, but exposes only one
ordinal in the current on-disk import table. Main `.text`/`.rdata` entropy is
approximately8 bits/byte, with a large `.merged` region, changed entry point,
and HASP components. These observations suggest protected/packed code; they do
not identify its exact protection algorithm. License-runtime thread names do not
establish acquisition threading. No unpacking, protection bypass, target launch,
memory dump, or active instrumentation was performed.

The installed sample workspace `Sample Files/Flight_Data_Demo_1.xml:42-45,8146-8148`
defines two LDNs with monitoring enabled on both. It also enables BC onLDN0 and
RT onLDN1. BC `BUSAORB` choices are transmit configuration, not proof of both-bus
monitor capture. The sample cannot be adopted as a passive configuration.

REA reported Ghidra unconfigured, Hopper unsupported on Windows, and IDA/Kuna
unconfigured. Static PE inspection and manual legacy call tracing were useful;
deep pseudocode and NxtGen acquisition topology remain unavailable here.

## AeroTrace comparison and actual SDK support boundary

| Area | Current source | Consequence |
| --- | --- | --- |
| Two channels and A/B | `modules/acquisition/providers/ddc_vendor_source.cpp:104-180,217,270` | Configure every selected LDN before starting producers; MTI options select buses and decoded status classifies records. |
| Polling | `ddc_vendor_source.cpp:197-284` | Fixed CH1-then-CH2 pass; at most one MTI packet per selected channel per poll; timeout0. This is shared servicing, not one capture thread per LDN. |
| Work ownership | `modules/acquisition/acquisition.cpp:73,174` | Poll, raw packet/ATD write, conversion, aggregation, checks and flush share a recording worker. GUI isolation does not eliminate disk/decode stalls. |
| UI | `apps/desktop/main_window.cpp:80,307,370,399` | Async readiness/futures and100ms GUI refresh already separate presentation from capture. |
| Connection lifetime | `acquisition.cpp:24,56,325` | Serialized ownership can move from a connection worker to a later recording worker, and close from the caller. Fixed OS-thread affinity is not established. |
| Cancellation | `modules/acquisition/providers/provider.cpp:45`, `ddc_source.cpp:86` | Poll wrapper does not propagate the supplied stop token; reads are nonblocking, but a vendor call is not preemptible. This is not a reproduced shutdown fault. |
| Time | `ddc_vendor_source.cpp:139,211,251` | Requests ACE_TT_2US; retains2000ns ticks, per-channel rollover state and raw data. Sequential starts do not establish synchronized clocks. |
| Stop/loss | `ddc_vendor_source.cpp:76,311`, `acquisition.cpp:259,323` | Explicit producer stop, bounded drain, final metrics and tail preservation; loss/integrity is separate from display eviction. |
| SDK discovery | `modules/acquisition/ddc_runtime.cpp:62,87` | Explicit SDK/DLL, installed SDK roots, then package/build fallback. Does not enumerate arbitrary PATH/System32 copies. Its runtime probe loads the DLL; it was not executed in this investigation. |
| Provider acceptance | `modules/acquisition/ddc_source.cpp:31,74`, `providers/provider.cpp:28` | x64 C ABI plus exact runtime hash required. One active provider connection per process; arbitrary mixed SDKs per channel are not supported by this host path. |
| Build inputs | `modules/acquisition/CMakeLists.txt:41`, `scripts/build-provider.ps1:11` | Existing provider machinery fingerprints selected vendor headers/runtime and supports offline contract checks, separate from bench acceptance. |

Paths without a prefix in the table refer to the same acquisition directory or
provider subdirectory indicated in their row.

The desktop enables all four stream selections by default; the low-level
`CaptureConfig` default is CH1A/B only (`source.hpp:15`; desktop explicit settings
at `main_window.cpp:316,352`). CLI/backend callers must not assume desktop defaults.

`modules/acquisition/providers/families.json:5` records prior provider evidence
for4.10.2 (runtime4.10.1.1) and4.4.2.4. Physical acceptance remains NOT RUN.
It historically marks3.6.15 blocked for missing inputs and has no4.9.5 family.
Finding installed3.6.15 inputs removes that particular absence; it does not
retroactively build/qualify a provider. Identical code sections with different
whole-file hashes are also not automatically interchangeable under this contract.

The prior30,026,073-record software fixture is useful application-path evidence.
`docs/Release-0.6.0.md:38` explicitly excludes SDK polling/vendor packet processing
from that direct fixture run. It cannot establish real SDK/driver four-stream
throughput or losslessness. No application tests were rerun for this research.

## Transferable engineering knowledge, before choosing a plan

- Preserve physical channel / logical assignment / A-B path / packet channel ID
  separately in acquisition, UI, logs and exports.
- Keep raw receive service independent of expensive presentation/analysis work;
  use finite packet pools/queues and explicit loss metrics. Whether disk writing
  should be separated and whether readers should be per-channel are decisions
  requiring workload and vendor concurrency evidence, not conclusions from names.
- Use the exact supported SDK headers and architecture inside each provider.
  Record DLL/driver/firmware identities and qualify the combination deployed.
  Legacy RTPro's x86 DLL cannot be copied into AeroTrace's x64 process.
- Retain real bus status, ticks, errors and raw packets before derived display
  work. Concurrent reception is distinct from synchronized timestamps.
- Use the installed3.6.15 headers/samples as development evidence; obtain readable
  4.9.5 headers before making layout-equivalence claims.
- Keep offline scientific/Python work separated from SDK ownership. NxtGen's
  packaging supports that division as an example, not proof of its scheduler.

RTips publishes a [Qt background-thread SDK example](https://www.rtipsonline.com/WebPages/Downloads/DDCResources/samples/rtpoll_Qt_gui/rtpoll_Qt_gui.html)
built with4.9.5. It is an **RT** example with transmit functionality, so it
corroborates Qt/worker integration rather than passive MT correctness.
Its [IRIG-B application note, AN0008](https://www.rtipsonline.com/WebPages/Downloads/DDCResources/documents/AN0008-IRIG-B-Synchronisation-Of-CH10-Bus-Monitors.pdf)
explains that independent monitor clocks require a shared time reference. Its
specific example is Linux4.9.5 with IRIG-capable boards; its results and patch
are not acceptance evidence for this Windows3.6.15 deployment.

## Question ledger and remaining evidence

| Requested question | Status / boundary |
| --- | --- |
| Identify supplied/installed applications, drivers and SDKs | Answered within the listed package roots, filtered Windows inventory and selected binary hashes. |
| Relate the supplied installers to installed applications | Selected main executable equality verified for both generations. All installer effects not audited. |
| How legacy RTPro supports background receive | Static worker-to-MTI-read path traced; live scheduling and two-instance operation unobserved. |
| How NxtGen schedules capture | Partially answered: nativeQt6x64+SDK dependency and two-LDN configuration; protected acquisition implementation unresolved. |
| How both channels/buses are represented | SDK declarations, sample settings and current AeroTrace source support the model; physical throughput unresolved. |
| Is arbitrary parallel SDK access safe | Unresolved; no generic exact-version reentrancy/affinity/lifetime contract established. |
| Which SDKs are interchangeable | Not established. Installed3.6.15 inputs identified;4.9.5 payload/header layouts and new provider acceptance remain unverified. |
| What hardware/driver is active now | No matching present DDC-class device/service observed; staged package versions known. Physical card/firmware/traffic not queried via SDK. |
| What should AeroTrace change | Existing mechanisms and evidence gaps identified; implementation plan intentionally deferred for the user's review. |

Detailed supplemental findings: `inventory-findings.md`, `sdk-findings.md`,
`aerotrace-findings.md`; identity manifests: `inventory/binaries.json`,
`sdk-source-identities.json`, `aerotrace-source-manifest.json`; static tracing:
`native-static-derived.json`, `legacy-focused-assembly.json`; installer evidence:
`supplied-installers.json`, `installer-payloads.json`, all under the task artifact
directory named above. Do not redistribute the adjacent extracted vendor archives
or SDK contents as AeroTrace source.
