param([string]$PackageName='AeroTrace1553-HardwareTest',[string]$OutputBaseName='AeroTrace1553-0.5.5-Setup-x64',[string]$Compiler=(Join-Path $env:LOCALAPPDATA 'AeroTraceBuildTools\InnoSetup\ISCC.exe'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$packageRoot=Join-Path $projectRoot ('dist\'+$PackageName)
foreach($required in @($Compiler,(Join-Path $packageRoot 'AeroTrace1553.exe'),(Join-Path $packageRoot 'deployment.json'),(Join-Path $packageRoot 'docs\Operator-Guide.pdf'))){if(!(Test-Path -LiteralPath $required)){throw ('Missing build input: '+$required)}}
$outputRoot=Join-Path $projectRoot 'dist\installers'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
& $Compiler /Qp ('/DPackageRoot='+$packageRoot) ('/DOutputRoot='+$outputRoot) ('/DOutputBaseFilename='+$OutputBaseName) (Join-Path $projectRoot 'installer\AeroTrace1553.iss')
if($LASTEXITCODE -ne 0){throw 'Offline installer compilation failed'}
$installer=Join-Path $outputRoot ($OutputBaseName+'.exe')
Get-FileHash -LiteralPath $installer -Algorithm SHA256 | ForEach-Object {"$($_.Hash)  $([IO.Path]::GetFileName($_.Path))"} | Set-Content -LiteralPath ($installer+'.sha256') -Encoding ASCII
Write-Output "Installer: $installer"
