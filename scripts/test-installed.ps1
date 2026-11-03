param([string]$InstallDirectory=(Join-Path $env:ProgramFiles 'AeroTrace1553'),[string]$ReportDirectory=(Join-Path $PSScriptRoot '..\out\installed-acceptance'))
$ErrorActionPreference='Stop'
$installedRoot=[IO.Path]::GetFullPath($InstallDirectory)
$reportRoot=[IO.Path]::GetFullPath($ReportDirectory)
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null
$exe=Join-Path $installedRoot 'AeroTrace1553.exe'
if(!(Test-Path -LiteralPath (Join-Path $installedRoot 'deployment.json'))){throw 'Strict deployment marker missing'}
$previousEnv=@{}
foreach($name in @('PATH','PYTHONHOME','PYTHONPATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM')){$previousEnv[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
function Run-Installed([string[]]$Arguments) {
  $escaped=@($Arguments | ForEach-Object {'"'+$_+'"'})
  $process=Start-Process -FilePath $exe -ArgumentList $escaped -WorkingDirectory ([IO.Path]::GetTempPath()) -WindowStyle Hidden -PassThru
  if(!$process.WaitForExit(180000)){$process.Kill();throw ('Installed process timeout: '+$Arguments[0])}
  if($process.ExitCode -ne 0){throw ('Installed workflow failed: '+$Arguments[0]+'; exit='+$process.ExitCode)}
}
try {
  $env:PATH="$env:WINDIR\System32;$env:WINDIR"
  $env:PYTHONHOME=$null;$env:PYTHONPATH=$null
  $env:QT_PLUGIN_PATH=$installedRoot;$env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $installedRoot 'platforms';$env:QT_QPA_PLATFORM='offscreen'
  Run-Installed -Arguments @('--automation-test',(Join-Path $reportRoot 'automation'))
  Run-Installed -Arguments @('--session-workflow-test',(Join-Path $reportRoot 'sessions'))
  Run-Installed -Arguments @('--workflow-test',(Join-Path $reportRoot 'pages'))
  Run-Installed -Arguments @('--demo-workflow-test',(Join-Path $reportRoot 'pdf'),(Join-Path $installedRoot 'examples\demo-icd\AeroTrace-Demo-ICD.pdf'))
  $acceptance=Get-Content -LiteralPath (Join-Path $reportRoot 'automation\acceptance.json') -Raw | ConvertFrom-Json
  if(!$acceptance.passed -or !(Test-Path -LiteralPath $acceptance.log_path)){throw 'Application journal or automation evidence missing'}
  $entries=@(Get-Content -LiteralPath $acceptance.log_path | ForEach-Object {$_ | ConvertFrom-Json})
  if($entries[0].event -ne 'application_start' -or $entries[-1].event -ne 'application_exit'){throw 'Lifecycle journal incomplete'}
  $captureEntries=@(Get-Content -LiteralPath (Join-Path $reportRoot 'automation\live-checks.atd.events.jsonl') | ForEach-Object {$_ | ConvertFrom-Json})
  if($captureEntries[0].run_id -ne $acceptance.run_id){throw 'Capture cannot be correlated to the application run'}
  $dataRoot=Split-Path (Split-Path $acceptance.log_path -Parent) -Parent
  & (Join-Path $installedRoot 'scripts\collect-diagnostics.ps1') -ApplicationDirectory $installedRoot -DataDirectory $dataRoot -SessionDirectory (Join-Path $reportRoot 'automation') -PreferencesPath $acceptance.preferences -OutputDirectory (Join-Path $reportRoot 'support')
  if(!(Test-Path -LiteralPath (Join-Path $reportRoot 'support.zip'))){throw 'Installed support bundle missing'}
  Add-Type -AssemblyName System.Drawing
  $icon=[Drawing.Icon]::ExtractAssociatedIcon($exe)
  if(!$icon){throw 'Native EXE icon missing'}
  $bitmap=$icon.ToBitmap();$bitmap.Save((Join-Path $reportRoot 'executable-icon.png'));$bitmap.Dispose();$icon.Dispose()
  $receipt=@{passed=$true;installedDirectory=$installedRoot;executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;cleanPath=$true;developmentFallbacksDisabled=$true;workspaceBatchChecks=$true;allPageWorkflow=$true;bundledPdfOcrRetrievalAndConversion=$true;runCorrelation=$true;supportBundle=$true;nativeIcon=$true;physicalHardwareTested=$false;note='Installed workflows on this host, with developer tool paths removed; not a separate clean-PC or physical-hardware qualification.'}
  $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot 'acceptance.json') -Encoding UTF8
  Write-Output 'PASS: installed workflows, bundled PDF runtime, native icon, journal correlation and support ZIP with clean PATH'
} finally {foreach($name in $previousEnv.Keys){[Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process')}}
