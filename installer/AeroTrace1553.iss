#ifndef PackageRoot
  #error PackageRoot must point to a complete verified deployment folder
#endif
#ifndef OutputRoot
  #define OutputRoot "..\dist\installers"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "AeroTrace1553-0.6.4-Review-Setup-x64"
#endif
#ifndef AppVersion
  #define AppVersion "0.6.4"
#endif
#ifndef CompleteBundle
  #define CompleteBundle 0
#endif
#if CompleteBundle
  #if !FileExists(PackageRoot + "\complete-bundle-approved.json")
    #error Complete bundle requires actual redistribution and physical qualification receipts. Run package.ps1 -BundleMode complete.
  #endif
#endif
[Setup]
AppId=AeroTrace1553_Desktop
AppName=AeroTrace 1553
AppVersion={#AppVersion}
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
DisableWelcomePage=yes
DisableDirPage=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
#if CompleteBundle
VersionInfoDescription=AeroTrace 1553 complete offline installer
#else
VersionInfoDescription=AeroTrace 1553 review installer - hardware qualification pending
#endif
VersionInfoProductName=AeroTrace 1553
VersionInfoVersion={#AppVersion}.0
VersionInfoTextVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}.0
VersionInfoProductTextVersion={#AppVersion}
InfoAfterFile={#PackageRoot}\Install-Notes.txt

[Files]
Source: "{#PackageRoot}\*"; DestDir: "{app}"; Excludes: "preferences.ini,Sessions\*,Diagnostics\*,test-results\*,__pycache__\*,complete-bundle-approved.json"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\AeroTrace 1553"; Filename: "{app}\AeroTrace1553.exe"; WorkingDir: "{app}"; IconFilename: "{app}\aerotrace.ico"
Name: "{group}\Operator guide"; Filename: "{app}\docs\Operator-Guide.html"
Name: "{group}\Collect Diagnostics"; Filename: "{app}\Collect Diagnostics.cmd"; WorkingDir: "{app}"
Name: "{group}\Inspect DDC prerequisites"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\scripts\ddc-prerequisites.ps1"" -Mode Inspect"; WorkingDir: "{app}"
Name: "{autodesktop}\AeroTrace 1553"; Filename: "{app}\AeroTrace1553.exe"; IconFilename: "{app}\aerotrace.ico"

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\AeroTrace1553.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\AeroTrace1553.exe"; Flags: uninsdeletekey

[Run]
Filename: "{app}\AeroTrace1553.exe"; Description: "Start AeroTrace 1553"; Flags: nowait postinstall skipifsilent; Check: CanLaunchApplication

; User data is outside {app}; no uninstall deletion rule targets recordings, profiles or logs.

[UninstallDelete]
; These private runtime trees contain generated Python caches, not user recordings.
Type: filesandordirs; Name: "{app}\document-runtime"
Type: filesandordirs; Name: "{app}\document-worker"
Type: dirifempty; Name: "{app}"

[Code]
var
  DriverRestartRequired: Boolean;

function CanLaunchApplication(): Boolean;
begin
  Result := not DriverRestartRequired;
end;

function NeedRestart(): Boolean;
begin
  Result := DriverRestartRequired;
end;

procedure InitializeWizard();
begin
  WizardForm.FilenameLabel.Visible := False;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Parameters: String;
begin
  if CurStep = ssPostInstall then begin
    Parameters := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\scripts\ddc-prerequisites.ps1') + '"';
#if CompleteBundle
    Parameters := Parameters + ' -Mode InstallMissing -Apply';
#else
    Parameters := Parameters + ' -Mode Inspect';
#endif
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Parameters, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
      Log('DDC prerequisite inspection could not start. No driver readiness is claimed.');
      if not WizardSilent then MsgBox('DDC prerequisite inspection could not start. Open Inspect DDC prerequisites from the Start menu. Existing vendor drivers were not changed by this installer.', mbInformation, MB_OK);
    end else if ResultCode = 3010 then begin
      DriverRestartRequired := True;
      Log('DDC driver reports restart required. Hardware remains unavailable until restart and reinspection.');
    end else if ResultCode <> 0 then begin
      Log('DDC prerequisites require attention. Receipt exit code: ' + IntToStr(ResultCode));
      if not WizardSilent then MsgBox('AeroTrace is installed. DDC prerequisites require attention; open Inspect DDC prerequisites from the Start menu. Hardware capture readiness has not been established.', mbInformation, MB_OK);
    end;
  end;
end;
