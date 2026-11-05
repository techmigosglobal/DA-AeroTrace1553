param([ValidateRange(1,16)][int]$Jobs=2,[string]$Compiler='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'resolve-inno-compiler.ps1')
$Compiler=Resolve-InnoCompiler $Compiler
$project=Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $project
try {
  $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
  $nativePacks='out/provider-packs-063-'+$stamp
  $helperPacks='out/helper-packs-063-'+$stamp
  & "$PSScriptRoot/build.ps1" -Jobs $Jobs -Test
  if($LASTEXITCODE -ne 0){throw 'Desktop build/tests failed'}
  & "$PSScriptRoot/build-default-provider-packs.ps1" -Destination $nativePacks
  if($LASTEXITCODE -ne 0){throw 'Native provider pack verification failed'}
  & "$PSScriptRoot/build-ddc-helper.ps1" -Test -PackDestination ($helperPacks+'/ddc-3.6.15-x86')
  if($LASTEXITCODE -ne 0){throw 'x86 helper verification failed'}
  & 'out/build/desktop-local/modules/acquisition/aerotrace_helper_integration_tests.exe' 'out/build/ddc-helper-x86-062/fixtures/ddc-3.6.15-x86/helper-pack.json'
  if($LASTEXITCODE -ne 0){throw 'x64 desktop to x86 helper fixture integration failed'}
  & python "$PSScriptRoot/helper-packs.py" ($helperPacks+'/ddc-3.6.15-x86')
  if($LASTEXITCODE -ne 0){throw 'Helper pack validation failed'}
  $package='AeroTrace1553-0.6.3-Review-'+$stamp
  & "$PSScriptRoot/package.ps1" -PackageName $package -BundleMode review -ProviderPacksRoot $nativePacks -HelperPacksRoot $helperPacks
  if($LASTEXITCODE -ne 0){throw 'Deployment staging failed'}
  & "$PSScriptRoot/build-installer.ps1" -PackageName $package -OutputBaseName 'AeroTrace1553-0.6.3-Review-Setup-x64' -Compiler $Compiler
  if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
} finally {Pop-Location}
