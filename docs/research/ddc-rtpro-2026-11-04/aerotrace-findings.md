# AeroTrace source comparison — 4 November 2026

Research only. No application source, build configuration, tests, runtime settings,
SDK installation, or hardware state was changed by this thread. No application
or provider binary was executed. Findings describe current source declarations,
not physical throughput or vendor application execution.

## Evidence and coverage

Source HEAD observed: `9955af7fc66b5ba3ebb8e9c76c34528a7bf1838c`.
The matching graph project is `AeroTrace1553-0.5.5-Source`; index status reports
1073 nodes / 5348 edges, generation `2026-11-02T15:33:00Z`. Parent searched the
first 35 of 47 DDC/Acquisition/Provider matches; this thread read the remaining
12. Actual MCP index status, graph snippet, and two coverage checks were used.
Current files have metadata changes; the provider subtree and RT monitor header
are not tracked in that generation; docs/scripts are excluded. The partial
ranges in acquisition/source declarations were read directly. All substantive
claims below come from current source reads rather than stale graph line ranges.
This is a bounded acquisition/provider/GUI-lifecycle review, not an exhaustive
code or threading audit.

## What already exists

| Question | Current observed implementation | Source |
| --- | --- | --- |
| Acquisition threading | One `std::jthread` owns an active recording loop; SDK polling, packet writing, ATD writing, conversion, RT aggregation, checks, publication and periodic flush all run serially on that worker. GUI consumes batches rather than one signal per record. | [declaration](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.hpp:31>), [worker](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:73>), [work loop](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:174>) |
| GUI isolation | Desktop updates on a 100 ms QTimer, starts connection/recording with futures, and polls readiness from `updateLive`; it does not synchronously await connection in the UI path. | [timer](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:80>), [connect](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:307>), [record](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:370>), [refresh](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:399>) |
| Two hardware channels | Channel indices 0/1 map to two configurable logical devices; distinct LDNs are required when both enabled. Open/configure all selected devices first; only then start each monitor. Failure in either setup closes the source without starting either. | [logical defaults](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/source.hpp:15>), [open/setup](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:104>), [start](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:177>) |
| Both buses per channel | Four software stream bits map `(channel-1)*2+bus`. MTI applies disable-A/disable-B options at configure; decoded block status identifies bus. Legacy MT reads both paths then applies selected software filter. Buses are not separate logical devices or worker threads. | [bus options](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:145>), [legacy filter](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:217>), [MTI filter](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:270>) |
| Desktop default | Device Setup reads all four channel/bus values with default true. The low-level `CaptureConfig` default is CH1 A/B only; desktop explicitly replaces it. | [backend default](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/source.hpp:15>), [desktop connect default](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:316>), [record default](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/apps/desktop/main_window.cpp:352>) |
| MT versus MTI | Passive MT identity open, read capabilities, then reinitialize as MTI when `HWVER_CAPABILITY_MTI` exists; otherwise legacy MT path. Required export sets checked separately. | [identity/protocol](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:122>), [required exports](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:52>) |
| Polling schedule | Fixed CH1 then CH2 iteration. At most one MTI packet read per selected channel per poll, timeout 0; unavailable/busy/timeout advances to the next channel. Legacy reads at most 256 raw messages per channel. A fully idle pass sleeps 1 ms. No adaptive rotation or separate channel worker appears in this reviewed provider path. | [poll start](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:191>), [legacy quota](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:198>), [nonblocking MTI](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:230>), [idle](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:299>) |
| Buffering | MTI config uses 128 KiB device buffer setting, 32 buffers, 65,536-byte blocks, interval/word/message/overflow event flags. Legacy uses double stacks and a 1024*40-word host buffer. | [legacy/MTI setup](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:148>) |
| Display loss versus recording | Raw packet sidecars and ATD batches are written before the bounded display queue. Queue saturation evicts oldest GUI copies and counts displayDrops. Source loss/overflow or write failure marks capture incomplete. | [disk first](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:178>), [queue](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:227>), [loss failure](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:255>) |
| RT view | Independent channel/RT/A-B latest values/counts are accumulated before GUI thinning. Each RT retains at most 128 route summaries. | [RT structure](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/rt_monitor.hpp:7>), [aggregation before queue](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:198>) |
| Engineering/plots | Up to 512 mappings, at most eight selected traces, 8192 retained samples per selected signal/stream, immutable snapshots at 50 ms cadence. These remain bounded independently of full disk recording. | [decoder](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/live_decoder.cpp:6>), [history](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/live_decoder.cpp:44>), [publish](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:209>) |

## Lifecycle and limitations relevant to simultaneous capture

1. **Concurrent capture does not require four application threads.** The source
   enables both physical-channel monitor producers; each channel reports A/B
   provenance in its records. The host alternates bounded nonblocking reads.
   Actual physical simultaneous operation still needs device evidence.
2. **Start ordering is not synchronized-start proof.** Both channels are configured
   before either starts, but `aceMT[I]Start` is called sequentially. The source has
   independent per-channel clocks and no observed common hardware trigger or
   cross-device clock calibration in this reviewed path. `ACE_NO_TT_RESET` is
   used on initialization. Nanosecond formatting is not nanosecond accuracy.
   Source: [open/timebase](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:122>),
   [start loop](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:180>).
3. **Slow work shares the capture worker.** Disk writes/flushes, packet decode,
   conversions and signal checks delay the next SDK poll. Bounded GUI queues
   protect recording from GUI load; they cannot hide arbitrarily long storage
   or decoding stalls. This is a structural risk to measure, not a reproduced
   loss event. Source: [loop](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:174>).
4. **SDK ownership is serialized, not fixed OS-thread lifetime.** Connection opens
   in a temporary worker and retains the source after that worker finishes.
   Recording reuses it in a new worker; idle disconnection closes it from the
   calling thread. Nothing here proves vendor thread affinity/reentrancy.
   Source: [retention](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:24>),
   [reuse](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:56>),
   [idle close](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:325>).
5. **Cancellation cannot preempt a vendor call.** Open/start check cancellation
   callbacks between operations. The provider wrapper ignores the incoming poll
   stop token and calls its vendor source with an empty token; that vendor loop
   uses bounded nonblocking reads. Stop requests wait for the current work to
   finish and join; no general vendor-call deadline is established by this code.
   Source: [callback](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_source.cpp:55>),
   [poll wrapper](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_source.cpp:86>),
   [provider poll](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/provider.cpp:45>),
   [stop](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:323>).
6. **Shutdown is explicit stop/drain/release.** Stop both producers, drain bounded
   buffered packets, sample final metrics, preserve tail and close on failure.
   Successful Stop recording retains the configured connection. Metrics are
   explicitly observed per-channel high-water counters; cumulative/reset vendor
   semantics are not asserted.
   Source: [metrics](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:76>),
   [drain](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:311>),
   [tail handling](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/acquisition.cpp:259>).
7. **Time semantics are deliberate.** Current adapter requests ACE_TT_2US and
   preserves device ticks with 2000 ns resolution. It extends MTI 48-bit RTC
   separately per channel. Classic short tags retain explicit silent-rollover
   ambiguity. Secondary/absolute MTI time format fails rather than fabricating
   a timestamp. Windows caller-owned packet buffer release -68 exception is
   restricted to the original supplied pointer.
   Source: [timebase](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:139>),
   [legacy time](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:211>),
   [MTI time/release](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/ddc_vendor_source.cpp:251>).

## SDK compatibility is a provider contract, not just a DLL search

- Runtime discovery admits valid x64 PE, common identity/time APIs and at least
  one complete MT or MTI receive export set. It considers explicit SDK/DLL,
  DDC_SDK_ROOT, installed SDKs (including uninstall registry), then packaged/build
  fallback. Invalid explicit selection fails; differing installed versions can
  require an exact user choice. It does not search arbitrary PATH DLLs.
  [discovery](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_runtime.cpp:62>),
  [precedence](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_runtime.cpp:87>).
- Discovery calls `LoadLibraryEx` and `aceGetLibVersion`; it is not a pure static
  metadata parser. This investigation did not run it against vendor binaries.
  [runtime probe](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_runtime.cpp:70>).
- A production provider is x64, C ABI v1; it contains vendor header-defined
  layouts internally and publishes fixed-width records. No C++/Qt/vendor object
  or allocator ownership crosses the boundary. One active provider connection
  is allowed per process, so simultaneously using different SDK families for
  separate channel pairs is not supported by this reviewed host path.
  [ABI](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/provider_abi.h:1>),
  [exclusive owner](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_source.cpp:74>).
- Production selection requires the selected runtime SHA-256 to match the
  provider's compiled identity, as well as a complete ABI. Provider also checks
  the runtime hash again on open. Merely having matching export names does not
  grant support to arbitrary SDKs.
  [selection](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_source.cpp:31>),
  [open validation](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/provider.cpp:28>).
- CMake builds providers from the selected licensed header tree, fingerprints
  every configured header and the runtime, and generates a descriptor. Existing
  helper builds, contract-tests and inspects a chosen provider without opening
  a card; physical qualification remains a separate state.
  [build rules](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/CMakeLists.txt:41>),
  [build helper](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/scripts/build-provider.ps1:11>).
- Recorded family evidence currently lists 4.10.2 (runtime4.10.1.1) and4.4.2.4 as
  vendor-build/fixture/runtime-inspection PASS, physical NOT RUN. It lists3.6.15
  as BLOCKED for missing inputs and does not list4.9.5. Those are historical
  records, not this PC's current inventory. Newly installed3.6.15 inputs may
  remove the missing-input prerequisite, but no3.6.15 or4.9.5 provider build,
  runtime acceptance or physical result was produced in this research thread.
  [family records](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/providers/families.json:5>),
  [current documentation](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/docs/provider-contract.md:33>).
- The SDK agent found multithreaded vendor samples but no explicit general
  per-LDN reentrancy or affinity guarantee. Therefore parallel SDK invocation
  is not established as an approved replacement for serialized calls. Refer to
  its vendor source evidence for exact declarations.

## Existing tests and their boundary

Current contract source covers all15 nonempty stream masks, two-channel setup
ordering/rollback, tail drain, loss/overflow, reconnect, provider ABI rejection,
exclusive ownership, directory layouts and fake API versions. These are
injected-fixture checks, not physical acceptance of the named SDK versions.
[contract tests](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_tests.cpp:15>),
[all stream masks](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/modules/acquisition/ddc_tests.cpp:114>).

Release report records the four-stream600-second software fixture,30,026,073
records at50,043.54/s, p95 GUI heartbeat90ms and maximum984ms. It explicitly says
SDK polling/vendor packet processing were not exercised in that direct software
traffic run. No lag-free or physical four-stream claim follows. No tests were
rerun for this source-only research.
[release identity](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/docs/Release-0.6.0.md:18>),
[measurements and caveats](<C:/Users/Vinay/Downloads/aero trace/AeroTrace1553-0.5.5-Source/AeroTrace1553-0.5.5-Source/docs/Release-0.6.0.md:38>).

## Questions status

- Answered from current source: acquisition concurrency topology; channel/LDN/A-B
  mapping; buffering/read policy; GUI separation; capture/stop/loss behavior;
  runtime/provider-selection compatibility boundaries.
- Partially answered: whether those choices satisfy sustained physical traffic
  and SDK3.6.15/4.9.5 on this PC. Inputs and source mechanisms are identifiable;
  build/runtime/bench evidence is separate and was not executed here.
- Unresolved in this source-only thread: RTPro internal execution/thread model,
  vendor general thread-safety contract, current physical bus traffic, shared
  clock accuracy and vendor driver/firmware qualification. Other investigation
  threads cover installed artifacts and vendor documentation.

No implementation plan or application change was made; these findings are input
for the user's requested later planning phase.


---
Preserved research appendix. References to adjacent raw JSON/logs in this original
thread report resolve under repository-root `out/investigations/ddc-rtpro-20261104/`.
Selected identity manifests are also preserved in this directory. The consolidated
report one directory above records the later installer-payload hash verification
and manual legacy native call tracing.
