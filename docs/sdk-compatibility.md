# DDC SDK compatibility policy and implementation

The goal is automatic integration across compatible aceXtreme SDK releases, with explicit evidence when a new adapter is needed. No version number is hard-coded as the only accepted runtime.

## Implemented

- Discover installed SDK roots, the configured SDK environment variable, registry install locations, packaged runtime and explicit manual runtime selection. Do not load arbitrary DLLs from PATH.
- Inspect x64 PE architecture and required base exports before card open; retain path, file/API version, origin and driver-version-match evidence.
- Report complete legacy MT and MTI receive interfaces independently, including missing export names in discovery JSON. MTI requires release and integrity-metrics functions as well as capture functions.
- Choose hardware MT/MTI from actual capability evidence. Missing functions for the selected hardware mode fail with the exact missing symbol. All selected channels configure before acquisition starts.
- Preserve raw packets, original ticks/resolution, selected API results, compiled structure sizes and per-channel loss/overflow evidence. Never load BC/RT transmit functions.
- SDK fixture tests exercise several reported API versions, wrong architecture, non-DDC/missing interfaces, all 15 nonempty A/B/channel masks, stop/drain and reconnect after injected faults. These fixtures share the compiled ABI; they do not prove those vendor releases are compatible.

## Selecting the SDK installed on each PC

No SDK folder version is fixed in the deployed application's runtime selector. In **Device setup → Technical details**, enter either the installed SDK folder or the exact x64 `emacepl.dll`. Leave it blank to use the machine configuration and automatic discovery.

Selection precedence:

1. Explicit application SDK-folder or DLL selection.
2. The application's inherited `DDC_SDK_ROOT` environment variable, pointing to the SDK installed on that PC.
3. Automatically discovered installed SDKs, preferring available driver-version evidence and requiring an unambiguous choice.
4. Packaged/build runtime fallback when no installed candidate can be selected.

Recognized folder layouts are `Lib/x64/Release/emacepl.dll`, `libraries/emacepl/bin/x64/Release/emacepl.dll`, or an explicitly selected runtime directory containing `emacepl.dll`. A configured folder that is missing or incompatible produces an error; it never silently switches to a packaged or other installed SDK. Conflicting versions or MT/MTI interfaces require an exact DLL choice. The selector loads dependencies from the chosen DLL directory and Windows system directory rather than arbitrary PATH entries.

For one launch, PowerShell can set the machine-specific choice in the launching process:

~~~powershell
$env:DDC_SDK_ROOT = 'D:/Vendor/DDC/<installed-sdk-folder>'
& 'C:/Program Files/AeroTrace1553/AeroTrace1553.exe'
~~~

Replace both paths with the local installation. Clear any explicit application override to use the environment choice, then Rescan DDC. This selects runtime software; Card Manager still supplies physical logical-device assignments. Source builds use `-DDDC_SDK_ROOT=<local-sdk>` to select licensed headers at configure time. A binary built without the adapter cannot gain hardware support merely by discovering a DLL.

The 3 November development tests cover all three folder layouts, explicit-over-environment priority, environment-over-auto priority, missing/invalid configured roots and conflicting interfaces. These are fixture tests, not qualification of arbitrary SDK ABIs.

## What automatic discovery cannot establish

Two DLLs may export the same function names while using different structure sizes, constants, calling conventions or timestamp semantics. Driver, card and firmware dependencies are also outside export detection. Runtime matching therefore has separate states: discovered, receive-interface complete, hardware open/configured, traffic observed and physically qualified. Only the final state justifies a declared supported combination.

## Extending to a new ABI family

1. Obtain the licensed vendor headers/runtime/driver/release notes for that family and the fitted card.
2. Compare function declarations and layouts with the build's contract; write an adapter for differences rather than casting incompatible structures.
3. Run the SDK fixture/record/replay/CSV tests using vendor sample vectors and actual traffic.
4. Run the [hardware matrix](SES-Bench-Checklist.md), including responding RTs, B paths, simultaneous channels, endurance and physical reconnect. Record exact runtime and driver hashes.
5. Add the qualified combination to the [compatibility matrix](compatibility-matrix.md).

Versioned provider DLLs now implement the [C ABI provider contract](provider-contract.md). Each provider is built against local vendor headers and fingerprints its runtime evidence. The installed 4.10.2 SDK is available for this development build; 3.6.15 vendor verification remains BLOCKED pending its licensed package. Additional SDKs require their own compiled provider and acceptance evidence. No universal future-SDK support is claimed.
