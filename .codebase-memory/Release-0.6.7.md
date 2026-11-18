# AeroTrace 1553 0.6.7 codebase memory

Recorded 8 November 2026 for graph project `AeroTrace1553-0.5.5-Source`.
See the [release record](../docs/Release-0.6.7.md) for installation,
verification, and qualification boundaries.

- **ICD review and conversion.** The PDF worker preserves explicit fields
  and unresolved evidence. `Window::reviewDocument` stages that evidence.
  `Window::publishProfile` validates and explicitly activates the mappings.
  The bundled 18-row preset remains simulation-only.
- **Simulation.** `Window::restoreCaptureSource` and
  `Window::selectCaptureSource` distinguish DDC, generic, example, and mapped
  ICD modes. `planRoutes` in the simulator validates stream restrictions,
  word counts, and field overlaps before generation. Acquisition decodes
  generated packets through the same path used by Engineering and Live Plot.
  The reported 0.6.6 run recorded 645 eight-word messages and decoded zero
  values. Its example profile expected RT 03, SA 12, receive, 32 words.
- **Bus Analysis and reports.** `Window::navigate` groups Inspector,
  Topology, and Alerts. The dashboard starts with no selected terminal.
  `Window::exportSignalReport` uses the common decoder for the engineering
  CSV and plot. It streams full saved ATD records with the latest saved
  profile and publishes the report folder after all outputs succeed.
- **Installer.** The bench-use and privacy acceptance precedes destination
  selection and installation. Inno reports native file progress. The build
  uses a temporary short-path junction for the staged Python payload.
  The installer reports product version 0.6.7 and its SHA-256 matched the
  sidecar.
- **Evidence.** All 35 desktop checks passed across the full run and
  navigation rerun. All eight document-worker tests and the native provider
  and x86 helper fixtures passed. The example test verified all four live
  plots and a 10,000-record report. Interactive isolated Windows setup and
  upgrade checks and physical DDC acceptance remain pending.

The refreshed fast graph has 1,895 nodes and 10,939 edges. Graph lookup
found the report export, source restore, and simulator route planning
entry points. Use source for excluded worker and installer scripts and
for C++ ranges flagged by the graph coverage check.
