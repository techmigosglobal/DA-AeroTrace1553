# AeroTrace 1553 0.6.9 bench candidate

The desktop shows a centered startup splash with the AeroTrace icon and a
bus-signal animation. It uses a static signal when Windows animations are
disabled and closes when the main window opens. The title bar, executable,
and installer report version 0.6.9.

ICD import accepts ordered PDF sets and compact tables with separate
message headers. Review keeps source pages, prefills typed fields, and
requests confirmation for undocumented conversion settings. ICD Chat offers
published mappings and PDF evidence as separate sources. Engineering and
plots use the published conversion. Live Plot displays configured limits
and marks out-of-range samples.

See [ICD ingestion and review](icd-documents.md) for the operator workflow.
The alternative screen layouts are available for design review. This build
uses the existing Qt layouts.

## Installation

Stop and save any active recording, then exit AeroTrace before upgrading.
The bench installer checks the registered version and preserves user data
and external DDC drivers during replacement. Unattended setup requires
`/ACCEPTBENCHNOTICE=0.6.9`.

Build the Windows x64 bench installer with
`powershell -ExecutionPolicy Bypass -File scripts/build-release-069.ps1`.

## Verification

The desktop and backend suite passed all 36 configured checks. The release
build verifies the provider fixtures and tests the staged splash at 100% and
150% scaling before compiling Setup. A read-only upgrade inspection
validated the existing installation.

Physical DDC engineering qualification remains pending.
