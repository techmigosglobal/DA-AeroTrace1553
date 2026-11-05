# AeroTrace 0.6.4 — selectable passive monitoring methods

Device Setup offers the native x64 provider and the separately packaged x86 helper, with automatic, MTI and classic MT receive modes. The operator selects each method explicitly; failures never trigger automatic switching. Connect configures the monitor, and Start recording begins capture.

The method controls are in `apps/desktop/forms/ddc_method.ui` for editing in Qt Designer. Existing backend and mode settings are retained. A helper pack must be software verified and matched to its runtime; physical compatibility is NOT RUN until bench evidence is recorded.

RTPro 3.6.13.1 is not included: matching authorized SDK headers, verified ABI and driver compatibility, and redistribution permission have not been established. The x86 SDK 3.6.15 helper is a separate compatibility method.

Build the review installer using `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-release-064.ps1`. It stages provider and helper packs, runs configured checks, compiles the Inno installer and writes a SHA-256 checksum. Hardware Connect, known-traffic capture and physical acceptance remain NOT RUN until verified on the affected system.
