#ifndef PackageRoot
  #error PackageRoot must point to a complete verified deployment folder
#endif
#ifndef OutputRoot
  #define OutputRoot "..\dist\installers"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "AeroTrace1553-0.5.5-Setup-x64"
#endif
[Setup]
AppId=AeroTrace1553_Desktop
AppName=AeroTrace 1553
AppVersion=0.5.5
AppPublisher=AeroTrace
DefaultDirName={autopf64}\AeroTrace1553
UsePreviousAppDir=no
DefaultGroupName=AeroTrace 1553
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir={#OutputRoot}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/fast
SolidCompression=yes
LZMAUseSeparateProcess=yes
SetupIconFile={#PackageRoot}\aerotrace.ico
UninstallDisplayIcon={app}\AeroTrace1553.exe
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoDescription=AeroTrace 1553 offline installer
VersionInfoProductName=AeroTrace 1553
InfoAfterFile=Install-Notes.txt

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PackageRoot}\*"; DestDir: "{app}"; Excludes: "preferences.ini,Sessions\*,Diagnostics\*,test-results\*,__pycache__\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\AeroTrace 1553"; Filename: "{app}\AeroTrace1553.exe"; WorkingDir: "{app}"; IconFilename: "{app}\aerotrace.ico"
Name: "{group}\Operator guide"; Filename: "{app}\docs\Operator-Guide.html"
Name: "{group}\Collect Diagnostics"; Filename: "{app}\Collect Diagnostics.cmd"; WorkingDir: "{app}"
Name: "{autodesktop}\AeroTrace 1553"; Filename: "{app}\AeroTrace1553.exe"; IconFilename: "{app}\aerotrace.ico"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\AeroTrace1553.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\AeroTrace1553.exe"; Flags: uninsdeletekey

[Run]
Filename: "{app}\AeroTrace1553.exe"; Description: "Start AeroTrace 1553"; Flags: nowait postinstall skipifsilent

; User data is outside {app}; no uninstall deletion rule targets recordings, profiles or logs.

[UninstallDelete]
; These private runtime trees contain generated Python caches, not user recordings.
Type: filesandordirs; Name: "{app}\document-runtime"
Type: filesandordirs; Name: "{app}\document-worker"
Type: dirifempty; Name: "{app}"
