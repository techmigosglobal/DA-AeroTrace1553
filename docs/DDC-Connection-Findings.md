# DDC connection findings — 30 October 2026

Current operation and qualification status: [0.5.5 status](current-status.md). Older machine-specific results below are historical evidence; they do not describe current hardware presence or qualify later builds.


## Confirmed evidence

The supplied SES AeroTrace 0.3.0 journals contain 20 hardware connection attempts, all rejected by `aceInitialize` with SDK error -82 (`ACE_ERR_DRIVER_OPEN`, unable to open device driver). The DLL loaded far enough to call the SDK and receive its error. No successful hardware monitor start or completed export is present in those journals. They do not identify the exact Windows open failure or the actual physical model.

Local Windows Driver Store inspection found signed DDC packages `acexpci`, `acexusb`, `e2mausb`, `emaebrpci` and `emapci`, version 4.10.2.1 dated 16 September 2026. Therefore “drivers not installed” is not an established diagnosis. A staged INF differs from an active binding to a connected device.

The local development PC has no connected DDC-class device, matching active kernel-driver service, or `HKLM/SYSTEM/CurrentControlSet/Control/DDC1553Devices` logical-assignment branch. The physical card is on SES. These local absences do not establish SES state.

The E2MA USB INF model table matches `USB\VID_4DDC&PID_5590` and installs service `e2mausb`. Other IDs must use the matching INF; do not bind a guessed driver to hardware. Card Manager manages the vendor logical-device assignments. The installed runtime contains the registry path and reads configuration via Windows registry APIs.

The packaged DLL matches the local SDK x64 DLL SHA-256 `5EC4E6A5AF2A4E53766645840F68E4092E53F5A02287526E58FEB961DCF045A5`. The SDK package says 4.10.2.1; the supplied DLL file version says 4.10.1.1. That difference alone is not evidence of a wrong DLL.

REA PE import inspection confirms an AMD64 DLL with imports from KERNEL32, ADVAPI32 and WS2_32, including CreateFile, DeviceIoControl, RegOpenKeyExA and RegQueryValueExA. It does not prove runtime driver binding or which import produced -82. REA deep analysis was unavailable because no supported decompiler provider was configured. Codebase-memory transport was also unavailable; material source claims were checked directly.

## Application issues addressed in 0.4.0

- The old scan only loaded the DLL; new Windows SetupAPI discovery checks actual present hardware and bindings, and records driver-store evidence and vendor logical assignments separately.
- The old adapter selected MTI unconditionally. SDK `mtpoll` and `mtirq` samples provide classic MT capture; `mti2disk` checks MTI capability. The updated adapter opens selected channels passively for identity, then chooses MT or MTI based on hardware capabilities. It does not select simulation on failure.
- MTI captures retain original Chapter 10 packets. Classic MT captures retain SDK 40-word records and source-specific device tick resolution; legacy timing ambiguity is marked explicitly.
- Initialization errors include the selected logical number, SDK operation/result and supplementary GetLastError context (which may be stale if the SDK did not set it). Rescan and support bundles supply the independent Windows evidence.
- Stop & save reads the finalized disk recording and exports both channel CSVs, independently of UI row retention and filters.

## What remains unknown on SES

Physical model and hardware ID; active driver binding and problem status; driver service state; logical-number mapping for each physical channel; device permissions; ownership by another analyzer; firmware compatibility; whether the legacy or MTI interface successfully initializes and receives the known bench traffic. The old journals do not resolve these questions. The new MT path fixes a compatibility gap but does not by itself prove the earlier driver-open failure is fixed.

Run `scripts/check-ddc.ps1` on SES or collect the updated application's support bundle. Follow `SES-Bench-Checklist.md`. Physical success requires increasing real hardware counts, loss/integrity checks, complete paired CSV totals and replay verification on that bench.
