#ifndef PackageRoot
  #error PackageRoot must point to a complete verified deployment folder
#endif
#ifndef OutputRoot
  #define OutputRoot "..\dist\installers"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "AeroTrace1553-0.7.1-Bench-Setup-x64"
#endif
#ifndef AppVersion
  #define AppVersion "0.7.1"
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
UsePreviousAppDir=yes
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
WizardSmallImageFile=..\apps\desktop\icons\aerotrace.png
DisableProgramGroupPage=yes
DisableWelcomePage=no
LicenseFile=Bench-Use-Notice.txt
WizardSizePercent=118,145
DisableDirPage=no
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
Source: "remove-previous-aerotrace.ps1"; Flags: dontcopy
Source: "Bench-Use-Notice.txt"; DestDir: "{app}"; Flags: ignoreversion
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
Type: filesandordirs; Name: "{app}\assistant-runtime"
Type: dirifempty; Name: "{app}"

[Code]
var
  DriverRestartRequired: Boolean;
  PreviousInstallDir: String;
  PreviousVersion: String;
  PreviousRegistered, AmbiguousRegistration, PerUserRegistration: Boolean;
  PreviousRemoved: Boolean;
  UpgradeMessage: String;
  StageLabels: array[0..4] of TNewStaticText;
  StageNames: array[0..4] of String;
  LogButton: TNewButton;
  ProgressText: TNewStaticText;
  DetailsButton: TNewButton;
  DetailsMemo: TNewMemo;
  LastFileStatus: String;
  PrerequisiteOutcome: String;
  SignalTimer: UINT_PTR;
  SignalImage: TBitmapImage;
  SignalPhase: Integer;
  SignalClosing: Boolean;

function SetTimer(hWnd: HWND; nIDEvent: UINT_PTR; uElapse: UINT; lpTimerFunc: UINT_PTR): UINT_PTR;
  external 'SetTimer@user32.dll stdcall';
function KillTimer(hWnd: HWND; nIDEvent: UINT_PTR): Boolean;
  external 'KillTimer@user32.dll stdcall';
function SystemParametersInfo(Action, Param: UINT; var Value: LongWord; Flags: UINT): Boolean;
  external 'SystemParametersInfoW@user32.dll stdcall';

procedure StopSignalAnimation();
begin
  if SignalTimer <> 0 then begin
    KillTimer(0, SignalTimer);
    SignalTimer := 0;
  end;
end;

procedure DrawSignal();
var
  X, Y, W, H, Pulse: Integer;
begin
  if SignalClosing or (SignalImage = nil) then exit;
  W := SignalImage.Width;
  H := SignalImage.Height;
  SignalImage.Bitmap.Width := W;
  SignalImage.Bitmap.Height := H;
  SignalImage.Bitmap.Canvas.Brush.Color := WizardForm.Color;
  SignalImage.Bitmap.Canvas.Pen.Color := WizardForm.Color;
  SignalImage.Bitmap.Canvas.Rectangle(0, 0, W, H);
  SignalImage.Bitmap.Canvas.Pen.Color := $00897648;
  SignalImage.Bitmap.Canvas.Pen.Width := ScaleX(2);
  Y := H div 2;
  SignalImage.Bitmap.Canvas.MoveTo(0, Y);
  for X := 0 to W do begin
    Pulse := (X div ScaleX(7)) mod 8;
    if (Pulse = 3) or (Pulse = 4) then
      SignalImage.Bitmap.Canvas.LineTo(X, Y - ScaleY(5))
    else SignalImage.Bitmap.Canvas.LineTo(X, Y);
  end;
  X := SignalPhase mod (W - ScaleX(6));
  SignalImage.Bitmap.Canvas.Brush.Color := $004D6228;
  SignalImage.Bitmap.Canvas.Ellipse(X, Y + ScaleY(4), X + ScaleX(5), Y + ScaleY(9));
  SignalImage.Invalidate;
end;

procedure SignalTimerProc(Arg1: HWND; Arg2: UINT; Arg3: UINT_PTR; Arg4: DWORD);
var
  Motion: LongWord;
begin
  if SignalClosing or WizardSilent then begin StopSignalAnimation(); exit; end;
  Motion := 0;
  if not SystemParametersInfo($1042, 0, Motion, 0) or (Motion = 0) then begin
    StopSignalAnimation(); SignalPhase := 0; DrawSignal(); exit;
  end;
  SignalPhase := SignalPhase + ScaleX(4);
  DrawSignal();
end;

procedure InitializeSignalAnimation();
var
  Motion: LongWord;
begin
  if WizardSilent then exit;
  SignalImage := TBitmapImage.Create(WizardForm);
  SignalImage.Parent := WizardForm;
  SignalImage.SetBounds(ScaleX(14), WizardForm.ClientHeight - ScaleY(39), ScaleX(138), ScaleY(22));
  SignalPhase := 0;
  DrawSignal();
  Motion := 0;
  if SystemParametersInfo($1042, 0, Motion, 0) and (Motion <> 0) then
    SignalTimer := SetTimer(0, 0, 80, CreateCallback(@SignalTimerProc));
end;

procedure DeinitializeSetup();
begin
  SignalClosing := True;
  StopSignalAnimation();
end;

function CanLaunchApplication(): Boolean;
begin
  Result := not DriverRestartRequired;
end;

function NeedRestart(): Boolean;
begin
  Result := DriverRestartRequired;
end;

const
  AeroTraceRegistration = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\AeroTrace1553_Desktop_is1';

procedure ReadPreviousInstallation();
var
  Root: Integer;
begin
  PreviousRegistered := RegKeyExists(HKLM64, AeroTraceRegistration) or RegKeyExists(HKLM32, AeroTraceRegistration);
  AmbiguousRegistration := RegKeyExists(HKLM64, AeroTraceRegistration) and RegKeyExists(HKLM32, AeroTraceRegistration);
  PerUserRegistration := RegKeyExists(HKCU, AeroTraceRegistration);
  if RegKeyExists(HKLM64, AeroTraceRegistration) then Root := HKLM64 else Root := HKLM32;
  RegQueryStringValue(Root, AeroTraceRegistration, 'InstallLocation', PreviousInstallDir);
  if PreviousInstallDir = '' then
    RegQueryStringValue(Root, AeroTraceRegistration, 'Inno Setup: App Path', PreviousInstallDir);
  RegQueryStringValue(Root, AeroTraceRegistration, 'DisplayVersion', PreviousVersion);
end;

function InitializeSetup(): Boolean;
var
  I: Integer;
begin
  Result := not WizardSilent;
  if not Result then
    for I := 1 to ParamCount do
      if CompareText(ParamStr(I), '/ACCEPTBENCHNOTICE={#AppVersion}') = 0 then
        Result := True;
  if not Result then
    Log('Unattended setup requires /ACCEPTBENCHNOTICE={#AppVersion}.');
  ReadPreviousInstallation();
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine;
  if PreviousRegistered then
    Result := Result + 'Replace AeroTrace ' + PreviousVersion + NewLine + Space + PreviousInstallDir + NewLine +
      Space + 'Setup will remove this version before installing. Stop and save any recording, then exit AeroTrace.' + NewLine
  else
    Result := Result + 'Install AeroTrace {#AppVersion}' + NewLine;
  Result := Result + NewLine + 'Recordings, imported ICDs and profiles remain in their existing locations.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) or (CurPageID = wpReady) then begin
    Result := (PreviousInstallDir = '') or
      (CompareText(RemoveBackslashUnlessRoot(WizardDirValue), RemoveBackslashUnlessRoot(PreviousInstallDir)) = 0);
    if not Result then begin
      Log('Use the registered destination for replacement. Remove the old version separately to relocate.');
      if not WizardSilent then
        MsgBox('Use the registered AeroTrace location for this replacement. To relocate, remove the old version through Installed apps first and keep your preferences and data.', mbInformation, MB_OK);
    end;
  end;
end;

procedure UpgradeOutput(const S: String; const Error, FirstLine: Boolean);
begin
  Log(S);
  if Trim(S) <> '' then UpgradeMessage := S;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Parameters, Target: String;
  ResultCode: Integer;
begin
  Result := '';
  Log('AeroTrace stage: prepare');
  WizardForm.PreparingLabel.Caption := 'Prepare · checking the installation destination';
  WizardForm.Update;
  if AmbiguousRegistration or PerUserRegistration then begin
    Result := 'Resolve multiple or per-user AeroTrace registrations through Windows Installed apps, then run setup again.';
    Exit;
  end;
  Target := RemoveBackslashUnlessRoot(WizardDirValue);
  if not PreviousRegistered then begin
    if FileExists(AddBackslash(Target) + 'AeroTrace1553.exe') or
       DirExists(AddBackslash(Target) + 'document-runtime') or
       DirExists(AddBackslash(Target) + 'document-worker') or
       DirExists(AddBackslash(Target) + 'providers') or
       DirExists(AddBackslash(Target) + 'helpers') then
      Result := 'AeroTrace application files already exist at this unregistered location. Choose a clean destination and retain any user data.';
    Exit;
  end;
  if PreviousRemoved then Exit;
  if PreviousInstallDir = '' then begin
    Result := 'The registered installation location is missing. Repair or remove the old version through Windows Installed apps.';
    Exit;
  end;
  Log('AeroTrace stage: remove previous version');
  WizardForm.PreparingLabel.Caption := 'Remove previous version · AeroTrace ' + PreviousVersion;
  WizardForm.Update;
  ExtractTemporaryFile('remove-previous-aerotrace.ps1');
  Parameters := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{tmp}\remove-previous-aerotrace.ps1') + '" -NewVersion {#AppVersion} -InstallDirectory "' + Target + '"';
  if not WizardSilent then Parameters := Parameters + ' -ShowProgress';
  UpgradeMessage := '';
  if not ExecAndLogOutput(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Parameters, ExpandConstant('{tmp}'), SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode, @UpgradeOutput) then begin
    Result := 'Could not start the previous-version check. Review the setup log.';
    Exit;
  end;
  if ResultCode <> 0 then begin
    NeedsRestart := ResultCode = 30;
    Result := UpgradeMessage;
    if Result = '' then Result := 'The previous version could not be removed. Review the setup log.';
    Exit;
  end;
  PreviousRemoved := True;
end;

procedure OpenSetupLog(Sender: TObject);
var
  ResultCode: Integer;
begin
  ShellExecAsOriginalUser('open', ExpandConstant('{log}'), '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
end;

procedure ShowStage(Current: Integer);
var
  I: Integer;
  Prefix: String;
begin
  for I := 0 to 4 do begin
    Prefix := '     ';
    if I < Current then Prefix := 'Done · '
    else if I = Current then Prefix := 'Now · ';
    StageLabels[I].Caption := Prefix + StageNames[I];
    StageLabels[I].Font.Style := [];
    if I = Current then StageLabels[I].Font.Style := [fsBold];
  end;
end;

procedure ToggleDetails(Sender: TObject);
begin
  DetailsMemo.Visible := not DetailsMemo.Visible;
  if DetailsMemo.Visible then DetailsButton.Caption := 'Hide &details'
  else DetailsButton.Caption := 'Show &details';
end;

procedure InitializeWizard();
var
  I, StageTop: Integer;
begin
  InitializeSignalAnimation();
  ProgressText := TNewStaticText.Create(WizardForm);
  ProgressText.Parent := WizardForm.InstallingPage;
  ProgressText.SetBounds(WizardForm.ProgressGauge.Left, WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(10), WizardForm.ProgressGauge.Width, ScaleY(24));
  ProgressText.Caption := 'Installing application files (0%)';
  DetailsButton := TNewButton.Create(WizardForm);
  DetailsButton.Parent := WizardForm.InstallingPage;
  StageNames[0] := 'Prepare';
  if PreviousRegistered then StageNames[1] := 'Remove previous version'
  else StageNames[1] := 'Previous version · fresh install';
  StageNames[2] := 'Install';
  StageNames[3] := 'Verify DDC prerequisites';
  StageNames[4] := 'Ready';
  StageTop := ProgressText.Top + ScaleY(24);
  for I := 0 to 4 do begin
    StageLabels[I] := TNewStaticText.Create(WizardForm);
    StageLabels[I].Parent := WizardForm.InstallingPage;
    StageLabels[I].SetBounds(ProgressText.Left, StageTop + ScaleY(15 * I), ScaleX(360), ScaleY(15));
  end;
  ShowStage(0);
  DetailsButton.SetBounds(ProgressText.Left, StageTop + ScaleY(80), ScaleX(106), ScaleY(24));
  DetailsButton.Caption := 'Show &details';
  DetailsButton.OnClick := @ToggleDetails;
  LogButton := TNewButton.Create(WizardForm);
  LogButton.Parent := WizardForm.InstallingPage;
  LogButton.SetBounds(DetailsButton.Left + DetailsButton.Width + ScaleX(8), DetailsButton.Top, ScaleX(106), ScaleY(24));
  LogButton.Caption := 'Open setup &log';
  LogButton.OnClick := @OpenSetupLog;
  DetailsMemo := TNewMemo.Create(WizardForm);
  DetailsMemo.Parent := WizardForm.InstallingPage;
  DetailsMemo.SetBounds(ProgressText.Left, DetailsButton.Top + ScaleY(30), WizardForm.ProgressGauge.Width,
    WizardForm.InstallingPage.ClientHeight - DetailsButton.Top - ScaleY(36));
  DetailsMemo.ScrollBars := ssVertical;
  DetailsMemo.ReadOnly := True;
  DetailsMemo.Visible := False;
  if PreviousInstallDir <> '' then WizardForm.DirEdit.Text := PreviousInstallDir;
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var
  FileStatus: String;
begin
  if MaxProgress > 0 then
    ProgressText.Caption := 'Installing application files (' + IntToStr((Int64(CurProgress) * 100) div MaxProgress) + '%)';
  FileStatus := WizardForm.StatusLabel.Caption + ' ' + WizardForm.FilenameLabel.Caption;
  if FileStatus <> LastFileStatus then begin
    LastFileStatus := FileStatus;
    if DetailsMemo.Lines.Count >= 200 then DetailsMemo.Lines.Delete(0);
    DetailsMemo.Lines.Add(FileStatus);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Parameters: String;
begin
  if CurStep = ssInstall then ShowStage(2);
  if CurStep = ssPostInstall then begin
    Log('AeroTrace stage: verify DDC prerequisites');
    ShowStage(3);
    ProgressText.Caption := 'Checking DDC prerequisites…';
    DetailsMemo.Lines.Add('Application files installed. Checking DDC prerequisites.');
    WizardForm.Update;
    Parameters := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\scripts\ddc-prerequisites.ps1') + '"';
#if CompleteBundle
    Parameters := Parameters + ' -Mode InstallMissing -Apply';
#else
    Parameters := Parameters + ' -Mode Inspect';
#endif
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Parameters, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
      PrerequisiteOutcome := 'DDC prerequisite inspection could not start.';
      DetailsMemo.Lines.Add(PrerequisiteOutcome);
      Log('DDC prerequisite inspection could not start. No driver readiness is claimed.');
      if not WizardSilent then MsgBox('DDC prerequisite inspection could not start. Open Inspect DDC prerequisites from the Start menu. Existing vendor drivers were not changed by this installer.', mbInformation, MB_OK);
    end else if ResultCode = 3010 then begin
      DriverRestartRequired := True;
      PrerequisiteOutcome := 'A DDC driver restart is required.';
      DetailsMemo.Lines.Add(PrerequisiteOutcome);
      Log('DDC driver reports restart required. Hardware remains unavailable until restart and reinspection.');
    end else if ResultCode <> 0 then begin
      PrerequisiteOutcome := 'DDC prerequisites require attention. See Inspect DDC prerequisites in Start.';
      DetailsMemo.Lines.Add(PrerequisiteOutcome);
      Log('DDC prerequisites require attention. Receipt exit code: ' + IntToStr(ResultCode));
    end else begin
      PrerequisiteOutcome := 'DDC prerequisite inspection completed. Verify hardware readiness in Device Setup.';
      DetailsMemo.Lines.Add(PrerequisiteOutcome);
    end;
    ProgressText.Caption := PrerequisiteOutcome;
    Log('AeroTrace stage: ready');
    ShowStage(4);
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and (PrerequisiteOutcome <> '') then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + PrerequisiteOutcome;
end;
