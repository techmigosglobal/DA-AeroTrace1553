param(
  [Parameter(Mandatory=$true)][string]$OutputDirectory,
  [Parameter(Mandatory=$true)][string]$DataDirectory,
  [ValidateRange(60,3600)][int]$Seconds=1800,
  [string]$QtRoot='C:\Qt\6.12.0\mingw_64',
  [string]$CompilerRoot='C:\Qt\Tools\mingw1310_64'
)
$ErrorActionPreference='Stop'
$developmentRoot=Split-Path $PSScriptRoot -Parent
$soakExecutable=Join-Path $developmentRoot 'out\build\desktop-release\apps\desktop\aerotrace_desktop.exe'
if(!(Test-Path -LiteralPath $soakExecutable)){throw 'Build the development application first.'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $DataDirectory -Force | Out-Null
$reportRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$captureRoot=(Resolve-Path -LiteralPath $DataDirectory).Path
$names=@('PATH','QT_QPA_PLATFORM','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','AEROTRACE_SOAK_SECONDS','AEROTRACE_SOAK_ROOT','AEROTRACE_KEEP_SOAK')
$previous=@{};foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
  $env:PATH=(Join-Path $QtRoot 'bin')+';'+(Join-Path $CompilerRoot 'bin')+';'+$env:PATH
  $env:QT_QPA_PLATFORM='offscreen'
  $env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins'
  $env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $QtRoot 'plugins\platforms'
  $env:AEROTRACE_SOAK_SECONDS=[string]$Seconds
  $env:AEROTRACE_SOAK_ROOT=$captureRoot
  $env:AEROTRACE_KEEP_SOAK='1'
  $receipt=@{source='simulation; no physical hardware';requestedSeconds=$Seconds;rate=50000;executable=$soakExecutable;executableSha256=(Get-FileHash -LiteralPath $soakExecutable -Algorithm SHA256).Hash;startedUtc=(Get-Date).ToUniversalTime().ToString('o');dataRoot=$captureRoot}
  $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot 'launch.json') -Encoding UTF8
  $soakProcess=Start-Process -FilePath $soakExecutable -ArgumentList @('--performance-test',('"'+$reportRoot+'"')) -WorkingDirectory $developmentRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $reportRoot 'stdout.txt') -RedirectStandardError (Join-Path $reportRoot 'stderr.txt')
  Write-Output ('Soak PID: '+$soakProcess.Id+'; report: '+$reportRoot)
  $soakProcess.WaitForExit();$soakProcess.Refresh()
  $receipt.exitCode=$soakProcess.ExitCode;$receipt.completedUtc=(Get-Date).ToUniversalTime().ToString('o')
  $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot 'launch.json') -Encoding UTF8
  if($soakProcess.ExitCode -ne 0){throw ('Soak failed; inspect '+$reportRoot)}
  Write-Output ('Soak passed: '+(Join-Path $reportRoot 'performance.json'))
} finally {
  foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
