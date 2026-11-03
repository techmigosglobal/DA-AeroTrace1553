param(
  [string]$QtRoot='C:\Qt\6.12.0\mingw_64',
  [string]$CompilerRoot='C:\Qt\Tools\mingw1310_64'
)
$ErrorActionPreference='Stop'
$developmentRoot=Split-Path $PSScriptRoot -Parent
$developmentExe=Join-Path $developmentRoot 'out\build\desktop-release\apps\desktop\aerotrace_desktop.exe'
foreach($required in @($developmentExe,(Join-Path $QtRoot 'bin\Qt6Widgets.dll'),(Join-Path $CompilerRoot 'bin\libstdc++-6.dll'))){if(!(Test-Path -LiteralPath $required)){throw ('Missing development dependency: '+$required)}}
$env:PATH=(Join-Path $QtRoot 'bin')+';'+(Join-Path $CompilerRoot 'bin')+';'+$env:PATH
$env:QT_QPA_PLATFORM='windows'
$env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH=Join-Path $QtRoot 'plugins\platforms'
$developmentProcess=Start-Process -FilePath $developmentExe -WorkingDirectory $developmentRoot -WindowStyle Normal -PassThru
Write-Output ('AeroTrace development PID: '+$developmentProcess.Id)
