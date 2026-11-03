# Compatibility and qualification matrix — 2 November 2026

| Combination / component | Evidence | Limit |
|---|---|---|
| Windows 11, BU-67113U, SDK 3.6.15, acexusb 3.6.15.1, Qt 6.11.1 MinGW x64 | Supplied November 2 SES loopback and 0.5.5 release notes | A-path loopback without responding RT; broader qualification pending |
| SDK 4.10.2, driver-store 4.10.2.1, Qt 6.12 | Older development-host software checks | No physical qualification; installed packages do not establish card presence |
| Qt 6.8.1 export build | Supplied recording-fix note | Export test only |
| Qt | Minimum 6.5, Widgets/Concurrent | Match Qt/Qwt/compiler ABI; Qt 6.0 unsupported |
| Build | C++20, CMake 3.24+, Ninja; MinGW 13.1 in supplied evidence | Pin and validate the complete toolchain |
| Qwt | Vendored 6.3.0 | Application CMake; upstream Qwt uses qmake |
| DDC discovery | x64, complete MT/MTI exports, selected installed/manual runtime | Export detection is not ABI/driver/firmware qualification |
| Documents | Isolated Python and pinned PDF/OCR/ONNX/model hashes | Representative ICD acceptance still required |
| Generative model | Optional loopback API | No bundled generative server/weights |
| Licensing | Optional PIV gate, disabled without configured certificate | Current delivery requires no physical key |
| Installation | Supplied 0.5.5 machine installer, Program Files | Administrator installation; signing and other machines require acceptance |
| RHEL | Target only | Application and physical drivers unqualified |

See [current behavior/evidence](current-status.md) and [hardware qualification](SES-Bench-Checklist.md). Record live inventory for each test.
