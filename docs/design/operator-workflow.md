# Operator workflow — 29 October 2026

## Intent and confirmed decisions

Make passive monitoring the default destination, give the table most of the workspace, reduce repeated setup/export dialogs and retain every existing feature. Preserve C++20, Qt Widgets, editable Designer forms, Qwt and CMake. No web wrapper, QML rewrite or Rust migration is introduced.

The confirmed roles remain Device 0 / Channel 1 = External and Device 1 / Channel 2 = Internal. The November 2 recording revision supersedes automatic capture: Connect configures only; Start recording is explicit; Stop & save retains the connection. Launch, arrival and configuration edits never record automatically. All selected channels configure before producers start. Default hardware paths remain 1A, 1B, 2A, 2B. See [the pending RT grouping proposal](rt-live-monitor-proposal.md).

## Layout and feature access

The 200 px sidebar collapses to 66 px, retaining icons, accessible names and tooltips. Primary destinations are Welcome, Device Setup, Capture Dashboard, Live Bus Monitor, Engineering, Live Plot, ICD Library and Sessions. More tools exposes Message Inspector, Bus Topology, Events & Alerts, Replay Workbench, ICD Signal Editor, Exports & Reports, License & Security, Help & Diagnostics, License Gate and Preferences. All 18 page bodies remain real widgets backed by `.ui` files.

The monitor has two per-device readiness summaries, Both channels / External / Internal tabs, one filter row, a large virtualized table, a compact selection/action strip and real queue/record/profile counters. Advanced packet fields remain available in Columns. Live batches retain the selected record while it remains in the bounded window. CSV saves asynchronously to a unique filename with checksum and profile sidecars. Its filtered scope is the retained analysis window; completed-session export remains available separately.

Ctrl+B collapses navigation; Ctrl+1/2/3 select monitor/engineering/plots; Ctrl+I imports a PDF or JSON profile. A single local PDF or JSON can also be dropped onto the workspace. Existing OCR, retrieval, rule validation, activation and manual review remain integrated.

## Visual system

Neutral warm gray `#f3f3f0` and white surfaces; graphite `#272d29`; muted green `#326344`; amber `#a4772e`; red `#b3483c`. No blue or purple accents. Body text is 15 px, table text 14 px and data rows 38 px. Native outline SVG icons, restrained corners and low-contrast separators keep emphasis on captured data. Plot traces use green, amber, charcoal and red.

The generated [concept](monitor-concept.png) is a design reference, not a screenshot of a running product. [Expanded implementation](monitor-implemented.png) and [collapsed implementation](monitor-collapsed.png) are actual Qt renders using clearly marked synthetic fixtures. The native packaged startup was also inspected at 1366×720 with real device errors and no invented traffic.

## Reference review

Visual samples were extracted locally from the supplied videos; no audio transcription or video editing was performed:

| Recording | Sample times | Relevant evidence |
|---|---|---|
| `2026-09-28 15-27-32.mkv` | 00:18, 00:58, 01:21, 01:52 | RT overview, monitor table, inspection and repeated export/overwrite interaction. |
| `2026-09-30 16-00-44.mkv` | 00:20, 01:35, 03:05, 03:27 | Separate device tabs, channel diagnostics, raw messages and CSV/stack export. |

The supplied DDC integration, development setup, PRD, research, example ICD, formula and reference-decision documents informed the review. The cached video-review archive was inventoried and its narrative review consulted. These documents describe another implementation; their Rust/IUP choices and unsupported completion claims are not imported into this application. Exact card identity remains unverified.

## Design-to-implementation review

| Check | Result |
|---|---|
| Table dominates the workspace | Passed in expanded/collapsed Qt renders and native 1366×720 view. Collapse increases actual table width by more than 100 px in the operator regression check. |
| Clear device identity and readiness | Separate External/Internal tabs and summaries match the confirmed mapping. Real startup shows both devices as not ready, unlike the concept's illustrative green state. |
| Reduced navigation clutter | Eight primary destinations, one More tools menu, icon collapse and keyboard shortcuts. The native menu was opened and all ten secondary destinations remained available. |
| Typography, spacing and palette | Larger rows/fonts, restrained native icons and neutral/green/amber/red theme reviewed. Qt native control metrics and Windows mnemonic underlines intentionally differ from the concept. |
| Monitor actions and data detail | One row selection supports inspector/engineering/plot access. Nine default columns reduce crowding; 17 metadata columns plus 32 individual word columns remain selectable. Word columns are grouped in a submenu. Headers and data align left. |
| Real operational information | Actual selected-device errors, counters and retained-window export scope replace illustrative numbers. Simulator fixtures are explicitly labeled. |
| Reversible workflow | Stop hold, delayed initialization/cancellation, per-device failure and row selection persistence have automated coverage. |

This is a structural implementation of the concept, not a pixel-identical image recreation. High-DPI combinations, screen-reader traversal, long translations and broad operator acceptance still need qualification.

The [live engineering](live-engineering.png) and [live plots](live-plots.png) renders show the actual nine-rule PDF workflow during explicit simulator capture. The engineering table includes raw words, values/units, card time and quality; its first eight cards are a summary. Plots select a signal and one channel/bus source, including mappings beyond the first eight. Duplicate page headings were removed and plot-axis tick counts reduced for larger typography.

## Evidence and limits

See `docs/validation.md`, `out/validation/redesign-release-tests.log`, `out/validation/redesign-core-tests.log` and `out/build/desktop-release/test-results/operator/operator-workflow.txt`.

The acquisition/record/decode owner, bounded queues, batching and min/max plot downsampling remain intact. Document work stays in its isolated process. The DDC test SDK proves that a second-device configuration failure starts neither producer and creates no raw capture. This does not prove physical hardware operation.

On this host, both real logical-device initializations currently return **Unable to open Win32 registry key (-80)**. Installed driver packages and a loadable runtime do not establish a present, configured device. Live dual-channel capture, physical disconnect/overflow behavior, clock alignment and sustained load remain bench acceptance work.
