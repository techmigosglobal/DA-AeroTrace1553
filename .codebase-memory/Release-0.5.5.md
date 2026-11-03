# AeroTrace 0.5.5 release — 2 November 2026

Application/project/resource/installer/deployment versions are 0.5.5. Machine installation is C:\Program Files\AeroTrace1553, registered in HKLM. Requires administrator installation. Prior per-user 0.5.0 installation was uninstalled; recordings, profiles, preferences and logs outside installation directories were retained.

Verified all 21 CTest tests and actual installed application clean-PATH workflows. Verified a complete install/uninstall round-trip in Program Files, including generated Python caches; verified final EXE ProductVersion/FileVersion, manifest version, package hashes, one 0.5.5 uninstall registration and machine App Paths registration.

Desktop deliverables: AeroTrace1553-0.5.5-Setup-x64.exe and AeroTrace1553-0.5.5-Source.zip, with SHA256 sidecars.

User CSVs: C:\Users\SES\Documents\AeroSpace\Exports. Corrected loopback verification CSVs: C:\Users\SES\Documents\AeroTrace-0.5.5-Release-Evidence\Loopback-CSVs. Source build/validation artifacts and earlier test recordings were moved out of the project into C:\Users\SES\Documents\AeroTrace-0.5.5-Build-Archive. The older validation notes' out/... evidence paths now resolve under that archive.

Source ZIP excludes build outputs, installers, machine-specific Qt Creator settings, obsolete prebuilt libraries, SDK installations and caches. Qt/Qwt and document dependencies are regenerated using scripts/build.ps1 and scripts/setup-documents.ps1 as needed. External licensed DDC SDK/driver remains installed separately.

Automatic runtime/driver discovery checks compatibility; arbitrary SDK ABI versions are not universally guaranteed. Physical loopback qualification remains Qt 6.11.1 MinGW x64, SDK3.6.15, acexusb3.6.15.1, BU-67113U. A 10-minute capture plus repeated bidirectional captures passed; this does not prove zero lag, every message type or indefinite driver stability.
