param([Parameter(Mandatory=$true)][string]$Pack,[string]$Destination='', [string]$Python='')
$ErrorActionPreference='Stop'
if(!$Destination){$Destination=Join-Path $env:LOCALAPPDATA 'AeroTrace1553/provider-packs'}
if(!$Python){
  $bundled=Join-Path (Split-Path $PSScriptRoot -Parent) 'document-runtime/python.exe'
  $Python=if(Test-Path -LiteralPath $bundled){$bundled}else{(Get-Command python -ErrorAction Stop).Source}
}
# No vendor setup, system DLL copy, driver install or application rebuild.
& $Python (Join-Path $PSScriptRoot 'provider-packs.py') stage --source $Pack --destination $Destination
if($LASTEXITCODE -ne 0){throw 'Provider pack was rejected; inspect the validation error'}
