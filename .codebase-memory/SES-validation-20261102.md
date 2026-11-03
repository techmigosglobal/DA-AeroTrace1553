# AeroTrace SES validation — 2 November 2026

Project: C:\Users\SES\Documents\AeroTrace1553-0.5.0-Whole-Project

## Completed

- Built with Qt 6.11.1 MinGW x64 and installed aceXtreme SDK 3.6.15.
- Rebuilt Qwt for the same Qt kit; removed reliance on another computer's Qwt/Qt build paths.
- Added SDK 3.6 layout support and handled no-data/timeout responses without failing acquisition.
- Application discovery detects one BU-67113U USB device, started acexusb driver 3.6.15.1, and Card Manager assignments 0 and 1.
- Both logical channels initialized in a ten-second passive monitoring check without reported SDK failure.
- Exported external/internal CSV files from that capture successfully. They have headers and zero data rows because no 1553 bus is connected.
- All 20 CTest checks passed. The installed app passed clean-PATH launch, GUI workflows, PDF processing, session export, and diagnostic ZIP collection.
- Fixed document runtime selection and diagnostic collection of vendor registry values.
- Built and installed dist\installers\AeroTrace1553-SES-Qt611-DDC315-Setup-x64.exe.
- Installed location: C:\Users\SES\AppData\Local\Programs\AeroTrace1553. Application, DDC runtime, Qwt and deployment manifest hashes matched the verified package. Windows App Paths registration was checked.
- Corrected the Qt Creator Qt 6 target to Release, out/build/desktop-local, aerotrace_desktop Run target. Prior settings retained in .qtcreator/CMakeLists.txt.user.before-ses-fix.

## Running from Qt Creator

Open the project's root CMakeLists.txt. Use Desktop Qt 6.11.1 MinGW 64-bit, Release, and aerotrace_desktop. The saved project settings have been updated. Reconfigure if prompted, then Run.

Use scripts/build.ps1 for a scripted build. When changing Qt versions, rebuild Qwt with the matching kit and use a fresh build directory. DDC_SDK_ROOT selects the licensed external SDK; packaging copies the runtime from the configured SDK.

## Verification evidence on the Windows PC

- out\build\desktop-local\Testing\Temporary\LastTest.log
- out\installed-final-acceptance\acceptance.json
- out\installed-final-acceptance\sessions\ddc-discovery.json
- out\installed-final-acceptance\support.zip
- out\investigation-20261102\ddc-live.atd and ddc-live-export.json
- out\investigation-20261102\verified-setup.log

## Limits

Qt 6.11.1 with SDK 3.6.15 is verified here. Every Qt 6 release, compiler ABI and DDC SDK version cannot be guaranteed interchangeable; other combinations require a matching build and validation. Real bus messages, timestamps, populated hardware CSVs, sustained load, and unplug/reconnect behavior remain untested because the card has no bus connected.
