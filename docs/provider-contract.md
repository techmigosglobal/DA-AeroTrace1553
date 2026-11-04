# Receive SDK provider contract — ABI v1

The desktop and portable acquisition client do not include vendor headers. Vendor-specific calls, structure layouts and error conversion live inside an x64 provider DLL built from licensed local SDK headers. This is an in-process extension: it does not isolate user-space vendor crashes or kernel-driver failures.

## Interface and ownership

`modules/acquisition/providers/provider_abi.h` is the public C ABI. `aerotrace_provider_get_api(1, sizeof(AtProviderApi))` must return the complete matching table or null. Every table/config/batch has an explicit size; record size and word offset have compile-time checks on both sides. The x64 C calling convention and natural alignment are required.

- Operations: open, update callbacks, start, poll, drain, metrics, release batch and close. Errors contain category/code, operation and bounded UTF-8 detail. Original SDK results also reach the diagnostic callback.
- Handles and poll buffers belong to the provider. The host copies normalized records/raw packets and calls release_batch before another receive or unload. No STL/Qt object, exception, vendor structure or allocator ownership crosses the ABI.
- Records contain fixed-width provenance and at most 32 words. A batch is bounded to 262,144 records / 512 raw packets; each raw packet is at most 65,536 bytes. Oversized or malformed output fails capture.
- SDK ownership is serialized through the capture lifecycle. Only one provider connection is allowed per process. Recording stop drains data while retaining an idle connection; changing providers requires releasing it. Callback ownership is cleared on retention and restored on the next run.
- Providers catch C++ exceptions at the API boundary. The host rejects incompatible ABI/architecture, incomplete interfaces and ambiguous providers. A DLL is trusted local application code, not a security sandbox.

## Runtime selection and compatibility evidence

The existing runtime selection order remains: application folder/DLL override, DDC_SDK_ROOT, unambiguous installed SDK discovery, packaged/build fallback. Provider selection searches the application `providers` directory or an explicit AEROTRACE_DDC_PROVIDER file/directory. Source builds may also use their compiled development directory; a deployment.json marker disables that development fallback. The provider_deployment_isolation regression verifies the boundary.

A production provider accepts the exact runtime SHA-256 recorded when it was built. Its descriptor records ABI, architecture, SDK identity, configured-header-tree fingerprint, runtime fingerprint and physical qualification status. Runtime names and version numbers alone do not establish ABI compatibility. A different runtime requires a provider built and verified against its SDK; do not edit the fingerprint merely to bypass this requirement.

Test providers live under `fixtures`, require explicit selection and refuse runtimes without the test-only marker. They are excluded from automatic selection and deployment. Fake reported API versions exercise discovery; they are not copies or qualification of vendor SDK versions.

## Building another SDK family

~~~powershell
./scripts/build-provider.ps1 -SdkRoot 'D:/DDC/<installed-sdk>' -ProviderId 'ddc-<family>' -Destination 'D:/AeroTrace/providers'
~~~

The script compiles a separate, uniquely named production DLL, descriptor and contract fixtures. It does not install drivers or transmit traffic. Licensed headers, runtimes and driver packages remain outside Git. Supply the appropriate runtime/driver/card/firmware evidence for the actual target PC.

Use -SdkIdentity for an explicit package label when the folder is named generically, for example -SdkIdentity aceXtremeSDK-4.4.2.4. -Runtime selects an exact runtime DLL from the supplied package when it differs from the standard SDK-folder location. The provider is compiled against the supplied headers, fingerprinted against that DLL, contract-tested, and inspected without opening a card. This does not establish compatibility with a different header/runtime combination. Use a separate provider ID and retain the package evidence for each combination.

4.10.2 is available locally. The supplied BU-69092S0 4.4.2.4 package has also been built as a separate provider, contract-tested and inspected against its x64 runtime. Both providers can coexist; selection follows the chosen runtime fingerprint. For 3.6.15, set AEROTRACE_SDK_3615_ROOT to the licensed SDK folder; until available, its vendor compilation and verification status is BLOCKED. The common provider contract and rejection fixtures are executable independently of that missing package. No universal future-SDK compatibility is claimed.

The supplied BU-69065 4.5.0 archive is a separate Tester/Simulator Menu product. Its TestSim32.dll files are COFF-i386, and matching x64 SDK headers/runtime were not supplied. This is a separate provider/architecture requirement, not another aceXtreme version. The x64 application rejects a 32-bit runtime instead of interpreting it through an incompatible interface. See [family evidence](../modules/acquisition/providers/families.json).

## Checks and qualification states

The contract suite covers ABI mismatch, missing entry point, wrong architecture, ambiguous providers, exclusive ownership, three SDK directory layouts, selection precedence, all 15 nonempty stream masks, rollback, raw provenance, clock boundaries, drain, loss/overflow and reconnect. `aerotrace_provider_info <SDK-folder-or-DLL>` verifies selection without opening a card.

Keep these states separate: provider compiled; contract fixtures passed; actual runtime inspected; physical connection established; expected traffic validated; hardware combination qualified. The descriptor starts with physical qualification NOT RUN. Fixtures and passive collector coverage cannot change that to PASS.

Use `scripts/collect-sdk-evidence.ps1 -SdkRoot <local-sdk> -OutputFile <new-json-file>` on each target PC to collect runtime/header hashes and Windows driver inventory without opening a card or copying licensed package contents. Keep machine-specific receipts outside Git. `families.json` records the initial family recipes and evidence boundaries.
