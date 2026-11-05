param([string]$PackageName='AeroTrace1553-0.6.4',[string]$OutputBaseName='',[string]$Compiler='')
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
# Inno's preprocessor receives each /D value as one argument. Keep the quotes
# inside the argument so paths containing spaces remain a single preprocessor
# value instead of being split by ISCC.
$defines=@(
  ('/DPackageRoot="'+$packageRoot+'"'),
  ('/DOutputRoot="'+$outputRoot+'"'),
  ('/DOutputBaseFilename="'+$OutputBaseName+'"'),
  ('/DAppVersion="'+$deployment.version+'"')
)
Push-Location $projectRoot
try {
  & $Compiler /Qp @defines $issFile
} finally {
  Pop-Location
}
if($LASTEXITCODE -ne 0){throw 'Offline installer compilation failed'}
$installer=Join-Path $outputRoot ($OutputBaseName+'.exe')
$versionInfo=(Get-Item -LiteralPath $installer).VersionInfo
$expectedVersion=[version]$deployment.version
if($versionInfo.FileMajorPart -ne $expectedVersion.Major -or $versionInfo.FileMinorPart -ne $expectedVersion.Minor -or $versionInfo.FileBuildPart -ne $expectedVersion.Build -or $versionInfo.FilePrivatePart -ne 0 -or $versionInfo.ProductVersion.Trim() -ne $deployment.version){throw 'Compiled installer version resources do not match the deployment'}
Get-FileHash -LiteralPath $installer -Algorithm SHA256 | ForEach-Object {"$($_.Hash)  $([IO.Path]::GetFileName($_.Path))"} | Set-Content -LiteralPath ($installer+'.sha256') -Encoding ASCII
Write-Output "Installer: $installer"
