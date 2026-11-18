param([string]$PackageName='AeroTrace1553-0.7.0',[string]$OutputBaseName='',[string]$Compiler='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'resolve-inno-compiler.ps1')
$Compiler=Resolve-InnoCompiler $Compiler
$projectRoot=Split-Path $PSScriptRoot -Parent
$packageRoot=Join-Path $projectRoot ('dist\'+$PackageName)
foreach($required in @($Compiler,(Join-Path $packageRoot 'AeroTrace1553.exe'),(Join-Path $packageRoot 'deployment.json'),(Join-Path $packageRoot 'docs\Operator-Guide.pdf'))){if(!(Test-Path -LiteralPath $required)){throw ('Missing build input: '+$required)}}
$outputRoot=Join-Path $projectRoot 'dist\installers'
$deployment=Get-Content -LiteralPath (Join-Path $packageRoot 'deployment.json') -Raw | ConvertFrom-Json
if($deployment.version -notmatch '^\d+\.\d+\.\d+$'){throw 'Deployment version is invalid'}
if(!$OutputBaseName){$OutputBaseName='AeroTrace1553-'+$deployment.version+'-Setup-x64'}
if((Get-Item -LiteralPath (Join-Path $packageRoot 'AeroTrace1553.exe')).VersionInfo.ProductVersion -ne $deployment.version){throw 'Deployment/executable versions differ'}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$issFile=Join-Path $projectRoot 'installer\AeroTrace1553.iss'
# Inno's payload reader can hit Windows path limits on deeply nested
# Python modules. A temporary junction shortens the path without copying
# or changing the verified deployment.
$payloadAlias=Join-Path ([IO.Path]::GetTempPath()) ('at-setup-'+[Guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Junction -Path $payloadAlias -Target $packageRoot | Out-Null
$defines=@(
  "/DPackageRoot=$payloadAlias",
  "/DOutputBaseFilename=$OutputBaseName",
  "/DAppVersion=$($deployment.version)"
)
try {
  & $Compiler /Qp @defines $issFile
} finally {
  # Nonrecursive removal deletes the junction itself.
  [IO.Directory]::Delete($payloadAlias)
}
if($LASTEXITCODE -ne 0){throw 'Offline installer compilation failed'}
$installer=Join-Path $outputRoot ($OutputBaseName+'.exe')
$versionInfo=(Get-Item -LiteralPath $installer).VersionInfo
$expectedVersion=[version]$deployment.version
if($versionInfo.FileMajorPart -ne $expectedVersion.Major -or $versionInfo.FileMinorPart -ne $expectedVersion.Minor -or $versionInfo.FileBuildPart -ne $expectedVersion.Build -or $versionInfo.FilePrivatePart -ne 0 -or $versionInfo.ProductVersion.Trim() -ne $deployment.version){throw 'Compiled installer version resources do not match the deployment'}
Get-FileHash -LiteralPath $installer -Algorithm SHA256 | ForEach-Object {"$($_.Hash)  $([IO.Path]::GetFileName($_.Path))"} | Set-Content -LiteralPath ($installer+'.sha256') -Encoding ASCII
Write-Output "Installer: $installer"
