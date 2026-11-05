# Installed DDC / RTPro inventory — 4 November 2026

Scope: read-only Windows installed-file, registry, CIM, PnPUtil, and PE-header
inventory on the current PC. No application, installer, driver, device handle,
capture session, debugger, or instrumentation was launched. Results below are
ordinary shell/static metadata evidence, not REA Evidence or runtime traces.
Structured results are in the adjacent `inventory/` directory.

## Installed identities

| Component | Observed installed artifact | Verified metadata |
| --- | --- | --- |
| DDC Card Manager | `C:/DDC/ddccm.exe` | x64, file/product 4.0.3.1, 2,683,432 bytes |
| aceXtreme C SDK | `C:/DDC/aceXtremeSDKv3.6.15` | Installed Apps 3.6.15; release Win32/x64 emacepl.dll both file/product 3.6.15.1 |
| RTPro legacy | `C:/RTips Technologies/RTProAnalyzer/RTProAnalyzer.exe` | x86, 3,344,896 bytes; no file version resource; Installed Apps 1.0 |
| RTPro NxtGen | `C:/RTips Technologies/RTProAnalyzerNxtGen/RTProAnalyzerNxtGen.exe` | x64, file 4.1.1.142 / product 4.1.1, 25,092,608 bytes |
| RTPro offline analysis | `C:/RTips Technologies/RTProAnalyzerNxtGen/RTProOfflineAnalysis/RTProOfflineAnalysis.exe` | x64, 23,704,219 bytes; no file version resource |

`inventory/binaries.json` contains exact SHA-256, size, PE machine, version,
product, and company metadata for 45 installed root binaries and selected SDK /
offline artifacts. `inventory/installed-apps.json` preserves the filtered registry
snapshot. PE machine was read from the on-disk COFF header, without loading DLLs.

Key main hashes:

- Legacy RTPro: `2B4A3DE447EB1BF83A6E45F4E4257F77C5402327463F2C6EEC80DF6DD0582DA4`.
- NxtGen: `C8BEC50FC39292FEC7F2B678A6D3D7DDCCB106372C296C5025540D746CB5B001`.
- SDK x64 release emacepl.dll: `71033E145FAA0E3D0303565CF15B2C274B4428CE91F53F3CF393C3A023511231`.
- SDK Win32 release emacepl.dll: `9A97CCB6FA8F7F865C4345951D6F9BFE0B7A679FCBE95F8CADB9D835F94DD647`.

The supplied folder label `DDC_4.0.3.1` matches **Card Manager**, not the installed
aceXtreme SDK version. NxtGen's actual executable confirms product4.1.1, despite
Installed Apps reporting4.1.0 and installed `components.xml`:3 reporting generic
ApplicationVersion1.0.0 / :10 package4.0. That XML's :12 InstallDate is2026-11-04.
Its ReadMe only lists revisions through3.4. Those installer/readme fields are
stale/inconsistent metadata, not evidence the executable is4.0 or4.1.0.

## Framework and runtime packaging

Legacy includes Qt5Core/Gui/Widgets/etc.5.13.1.0 x86, x86 qwt.dll, MinGW DW2 /
libstdc++ / winpthread, `cm.dll`, `CMServer.exe`, and HASP/Rockey licensing DLLs.
Its application-local x86 emacepl.dll is **3.6.13.1** (612,936 bytes), SHA-256
`D32D19CC7A31443A504AE3E1B34D103BB3411AD122FAF6CD22652D0BC1A3733F`.
Do not treat that copy as the installed3.6.15 SDK runtime.

NxtGen includes Qt6.10.2.0 x64 Core/Gui/Widgets/etc., MinGW SEH/libstdc++ /
winpthread, HASP runtime libraries, a `python/` embedded runtime, and a separate
`RTProOfflineAnalysis/` application. Both embedded Python executable and offline
python314.dll identify3.14.2. The offline `_internal` tree includes PyQt6,
pyqtgraph, NumPy2.5.0 metadata, pandas, SQLite, and related libraries. The root
`script.py` contains sample math/trigonometry/flight-simulation functions.
File presence establishes packaging, not that a particular feature executes it.

## DDC runtime search candidates

No `emacepl.dll` was found recursively inside the installed NxtGen tree. Existing
default-location candidates, read without LoadLibrary:

| Path | Version/machine | SHA-256 |
| --- | --- | --- |
| `C:/Windows/System32/emacepl.dll` | 3.6.15.1 x64, 654,528 bytes | `74CFB017FA96DC102184430F97D58553930F5D2348C2602D99022E4DBD60ABFE` |
| `C:/Windows/SysWOW64/emacepl.dll` | 3.6.15.1 x86, 622,272 bytes | `D47C11762C3DFB9CFBC25D9B3C7ECDBA9863960653E6182B2BF67096963D18CE` |

These version/size-matched system copies have different whole-file hashes from
the SDK release copies. A section-by-section comparison found **identical bytes
in every PE section**, including .text/.rdata/.data/.rsrc/.reloc and x64.pdata.
Differences occur in header checksum bytes and after the mapped section ranges;
signing/overlay differences are a plausible explanation, not authenticated fact.
See `inventory/sdk-system-runtime-diff.json`. Static candidates do not prove a
particular DLL is loaded; actual process loader evidence remains unobserved.

## Driver packages versus active hardware

Read-only `pnputil /enum-drivers` identified **ten staged DDC packages**:

| Driver | 3.6.15.1, dated2018-03-29 | 4.10.2.1, dated2026-09-16 |
| --- | --- | --- |
| acexpci.inf | oem71.inf | oem59.inf |
| acexusb.inf | oem70.inf | oem60.inf |
| e2mausb.inf | oem72.inf | oem61.inf |
| emaebrpci.inf | oem73.inf | oem62.inf |
| emapci.inf | oem74.inf | oem63.inf |

All list Data Device Corporation, class DDC Devices
`{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}` and Microsoft Windows Hardware
Compatibility Publisher as signer in PnPUtil metadata. New packages list
Declarative/Attested, older ones Legacy/Attested. This inventory did not perform
independent signature validation. The ten corresponding DriverStore SYS file
version resources match those package versions; exact paths/hashes are in
`inventory/driver-store-files.json` and filtered output in
`inventory/driver-store-ddc.txt`.

At the query time:

- `pnputil /enum-devices /class {b8fd1ebc-0bfd-4973-9a61-258d72bda69f}` reported
  **No devices were found on the system** (`inventory/ddc-class-devices.txt`).
- CIM Win32_PnPEntity and Win32_PnPSignedDriver filters for DDC/Data Device /
  aceXtreme/1553/BU-67/BU-69 had no matching devices (`pnp.json`,
  `signed-driver-devices.json`).
- Win32_SystemDriver and CurrentControlSet Services queries found no exact
  known DDC service names or driver-file paths. The usual five SYS files were
  absent from `C:/Windows/System32/drivers/`; they do exist in DriverStore.

Therefore the observed evidence supports **staged drivers and installed SDKs**,
not a currently enumerated DDC device, running DDC driver, or four-stream
hardware test. Multiple staged driver versions do not establish which version
would bind to a connected card. No hardware enumeration API in the SDK was called.

## Configuration and saved sample evidence

Bounded recursive registry inspection found empty-valued product keys under
`HKLM/SOFTWARE/WOW6432Node/Data Device Corporation/.../3.6.15.1` and
`HKCU/SOFTWARE/RTips Technologies/RTProAnalyzerNxtGen`. No actual Card Manager
logical assignments were recovered in these scopes. This is not a global
registry absence claim. Snapshots: `registry-config-python.json`.

Installed readmes at both RTPro roots describe selecting a device number,
starting/stopping monitoring, and opening recorded `.db` files (ReadMe.txt:22,
:26). This is vendor documentation, not observation of database schema or loss
behavior. NxtGen ReadMe.txt:59-62 records a historical microsecond-field defect
and a revision3.4 fix; that statement is not timestamp qualification of the
installed4.1.1 binary.

NxtGen's four installed sample XMLs have root `RTPRO_ANALZER`, two DEVICE nodes
named1553 Channel(LDN:0) and1553 Channel(LDN:1), and device-specific settings.
For `Sample Files/Flight_Data_Demo_1.xml`, DEVICES is at:42; LDN0 DEVICE:43 and
settings:45; LDN1 DEVICE:8146 and settings:8148. Parsed settings include LDN,
BC_ENABLED, MT_ENABLED, RTS_ENABLED, MONITOR_FILTER_ENABLED,
MTI_CONFIGURE_OPTION, MULTIFUNCTION_CARD, and counter-interrupt options.

That sample enables MT on both LDNs, **BC on LDN0 and RTS on LDN1**. It is a
transmit-capable demonstration, not a passive-monitor template for AeroTrace.
Its `BUSAORB` fields select A for configured BC messages; they do not prove
monitor filtering or simultaneous monitoring of A/B. See the bounded parsed
schema extract in `inventory/sample-workspaces.json`.

## Implications and limits

- Preserve the distinction among SDK version, Card Manager version, user-space
  DLL architecture/version, staged kernel package version, actual bound driver,
  and card firmware/capability. The evidence shows these values can differ.
- AeroTrace x64 must use the x64 SDK/provider path. Copying legacy RTPro's x86
  application-local runtime into an x64 process cannot establish compatibility.
- Installed3.6.15 headers/libs are now available for a future provider build;
  earlier reports that these inputs were unavailable are historical.
- NxtGen demonstrates a packaged native Qt6 application plus separate Python
  analysis tooling, consistent with keeping acquisition ownership independent
  from document/scientific/UI jobs. Actual scheduling and capture thread count
  still require code/runtime evidence, not library filenames.
- No live process, channel reception, bus-B behavior, throughput, loss handling,
  timestamp accuracy, SDK cross-version compatibility, or driver binding was
  tested. No implementation changes were made by this inventory.


---
Preserved research appendix. References to adjacent raw JSON/logs in this original
thread report resolve under repository-root `out/investigations/ddc-rtpro-20261104/`.
Selected identity manifests are also preserved in this directory. The consolidated
report one directory above records the later installer-payload hash verification
and manual legacy native call tracing.
