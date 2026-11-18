param([ValidateRange(1,16)][int]$Jobs=2,[string]$Compiler='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'resolve-inno-compiler.ps1')
$Compiler=Resolve-InnoCompiler $Compiler
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$nativePacks="out/provider-packs-069-$stamp"
$helperPacks="out/helper-packs-069-$stamp"
$package="AeroTrace1553-0.6.9-Bench-$stamp"
Push-Location -LiteralPath $project
try {
  & "$PSScriptRoot/build.ps1" -Jobs $Jobs -Test
  if($LASTEXITCODE -ne 0){throw 'Desktop build or tests failed'}
  & "$PSScriptRoot/build-default-provider-packs.ps1" -Destination $nativePacks
  if($LASTEXITCODE -ne 0){throw 'Native provider pack verification failed'}
  & "$PSScriptRoot/build-ddc-helper.ps1" -Test -PackDestination "$helperPacks/ddc-3.6.15-x86"
  if($LASTEXITCODE -ne 0){throw 'x86 helper verification failed'}
  & 'out/build/desktop-local/modules/acquisition/aerotrace_helper_integration_tests.exe' 'out/build/ddc-helper-x86-062/fixtures/ddc-3.6.15-x86/helper-pack.json'
  if($LASTEXITCODE -ne 0){throw 'x64 desktop to x86 helper fixture integration failed'}
  & "$PSScriptRoot/package.ps1" -PackageName $package -BundleMode review -ProviderPacksRoot $nativePacks -HelperPacksRoot $helperPacks
  if($LASTEXITCODE -ne 0){throw 'Deployment staging failed'}

  $savedPath=$env:PATH
  $savedPlatform=$env:QT_QPA_PLATFORM
  $savedScale=$env:QT_SCALE_FACTOR
  try {
    $env:PATH="$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM='offscreen'
    $executable=Join-Path $project "dist/$package/AeroTrace1553.exe"
    foreach($scale in '1','1.5') {
      $env:QT_SCALE_FACTOR=$scale
      $receipt=Join-Path $project "out/test-results/069-splash-$stamp/$scale"
      $process=Start-Process -FilePath $executable -ArgumentList "--startup-splash-test `"$receipt`"" -WindowStyle Hidden -Wait -PassThru
      if($process.ExitCode -ne 0){throw "Packaged startup splash check failed at scale $scale"}
      Get-Content -LiteralPath (Join-Path $receipt 'startup-splash.txt')
    }
  } finally {
    $env:PATH=$savedPath
    $env:QT_QPA_PLATFORM=$savedPlatform
    $env:QT_SCALE_FACTOR=$savedScale
  }
  & "$PSScriptRoot/build-installer.ps1" -PackageName $package -OutputBaseName 'AeroTrace1553-0.6.9-Bench-Setup-x64' -Compiler $Compiler
  if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
} finally {
  Pop-Location
}
