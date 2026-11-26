param([ValidateRange(1,16)][int]$Jobs=2,[string]$Compiler='',[switch]$UseVerifiedBuild,
      [string]$ResumeReportDirectory='',
      [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$OutputBaseName='AeroTrace1553-0.7.3-Bench-Setup-x64')
$ErrorActionPreference='Stop'
$arguments=@{Jobs=$Jobs;Compiler=$Compiler;Version='0.7.3';OutputBaseName=$OutputBaseName}
if($UseVerifiedBuild){$arguments.UseVerifiedBuild=$true}
if($ResumeReportDirectory){$arguments.ResumeReportDirectory=$ResumeReportDirectory}
& (Join-Path $PSScriptRoot 'build-release-071.ps1') @arguments
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
