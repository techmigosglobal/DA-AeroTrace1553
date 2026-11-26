param([ValidateRange(1,16)][int]$Jobs=2,[string]$Compiler='',[switch]$UseVerifiedBuild,
      [string]$ResumeReportDirectory='',
      [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version='0.7.1',
      [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$OutputBaseName='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'resolve-inno-compiler.ps1')
$Compiler=Resolve-InnoCompiler $Compiler
$project=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$versionTag=$Version.Replace('.','')
if(!$OutputBaseName){$OutputBaseName="AeroTrace1553-$Version-Bench-Setup-x64"}
$package="AeroTrace1553-$Version-Bench-$stamp"
$reports=Join-Path $project "out/test-results/$versionTag-$stamp"
if($ResumeReportDirectory){$reports=(Resolve-Path -LiteralPath $ResumeReportDirectory).Path}
$base=$OutputBaseName
Push-Location -LiteralPath $project
try {
  New-Item -ItemType Directory -Force -Path $reports | Out-Null
  $previousPath=$env:PATH
  $previousPlatform=$env:QT_QPA_PLATFORM
  $previousScale=$env:QT_SCALE_FACTOR
  $previousCharts=$env:AEROTRACE_MANUAL_CHART_SOAK
  $previousDuration=$env:AEROTRACE_SOAK_SECONDS
  $previousReferenceCompact=$env:AEROTRACE_REFERENCE_COMPACT
  $env:PATH="C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;$env:PATH"
  try {
    if(!$UseVerifiedBuild) {
      & "$PSScriptRoot/build.ps1" -Jobs $Jobs
      if($LASTEXITCODE -ne 0){throw 'Desktop build failed'}
    }
    $build=Join-Path $project 'out/build/desktop-local'
    $exe=Join-Path $build 'apps/desktop/aerotrace_desktop.exe'
    if((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $Version){throw "Expected the $Version desktop build"}
    $appHash=(Get-FileHash -LiteralPath $exe).Hash
    if($ResumeReportDirectory) {
      if((Get-Content -LiteralPath "$reports/native.sha256" -Raw).Trim() -ne $appHash){throw 'Native test receipt belongs to another executable'}
      [xml]$native=Get-Content -LiteralPath "$reports/native.xml" -Raw
      $cases=$native.SelectNodes('//testcase')
      if($cases.Count -lt 43 -or $native.SelectSingleNode('//failure|//error|//skipped')){throw 'Expected all 43 native checks to pass'}
    } else {
      ctest --test-dir $build --output-on-failure --output-junit "$reports/native.xml"
      if($LASTEXITCODE -ne 0){throw 'Desktop regression checks failed'}
      Set-Content -LiteralPath "$reports/native.sha256" -Value $appHash -Encoding ASCII
    }
    $env:QT_QPA_PLATFORM='offscreen'
    $env:AEROTRACE_MANUAL_CHART_SOAK='1'
    $env:AEROTRACE_SOAK_SECONDS='75'
    $run=Join-Path $reports $(if($ResumeReportDirectory){"capture-$stamp"}else{'capture'})
    $process=Start-Process -FilePath $exe -ArgumentList "--performance-test `"$run`"" -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw "32-chart capture fixture failed. See $run/performance.json"}
    $capture=Get-Content -LiteralPath "$run/performance.json" -Raw | ConvertFrom-Json
    if(!$capture.passed -or !$capture.exact_csv_contents -or $capture.live_command_fields -ne 32){throw 'Capture receipt did not meet release gates'}
    $nativePacks="out/provider-packs-$versionTag-$stamp"
    $helperPacks="out/helper-packs-$versionTag-$stamp"
    & "$PSScriptRoot/build-default-provider-packs.ps1" -Destination $nativePacks
    if($LASTEXITCODE -ne 0){throw 'Native provider pack verification failed'}
    & "$PSScriptRoot/build-ddc-helper.ps1" -Test -PackDestination "$helperPacks/ddc-3.6.15-x86"
    if($LASTEXITCODE -ne 0){throw 'x86 helper verification failed'}
    & "$PSScriptRoot/package.ps1" -PackageName $package -BundleMode review -ProviderPacksRoot $nativePacks -HelperPacksRoot $helperPacks
    if($LASTEXITCODE -ne 0){throw 'Deployment staging failed'}
    $root=Join-Path $project "dist/$package"
    $app=Join-Path $root 'AeroTrace1553.exe'
    if((Test-Path -LiteralPath "$root/assistant-runtime") -or
       @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {$_.Extension -eq '.gguf' -or $_.Name -match 'minilm|llama-server'}).Count) {
      throw 'Generative or embedding assets were found in the package'
    }
    $env:PATH="$env:SystemRoot/System32;$env:SystemRoot"
    $pdf=Join-Path $project 'examples/demo-icd/AeroTrace-Demo-ICD.pdf'
    $checks=@()
    foreach($scale in '1','1.5') {
      $env:QT_SCALE_FACTOR=$scale
      $result=Join-Path $reports "package-$scale"
      $job=Start-Process -FilePath $app -ArgumentList "--manual-mapping-test `"$result`" `"$pdf`"" -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
      if($job.ExitCode -ne 0){throw "Packaged mapping/Assistant check failed at scale $scale"}
      $splash=Start-Process -FilePath $app -ArgumentList "--startup-splash-test `"$result`"" -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
      if($splash.ExitCode -ne 0){throw "Packaged startup check failed at scale $scale"}
      $env:AEROTRACE_REFERENCE_COMPACT='0'
      $reference=Start-Process -FilePath $app -ArgumentList @('--operator-workflow-test',('"{0}"' -f "$result/reference"),('"{0}"' -f $pdf)) -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
      if($reference.ExitCode -ne 0){throw "Packaged operator reference check failed at scale $scale"}
      $env:AEROTRACE_REFERENCE_COMPACT='1'
      $compact=Start-Process -FilePath $app -ArgumentList @('--operator-workflow-test',('"{0}"' -f "$result/compact"),('"{0}"' -f $pdf)) -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
      if($compact.ExitCode -ne 0){throw "Packaged compact operator check failed at scale $scale"}
      $checks+=@{scale=$scale;mapping=$job.ExitCode;startup=$splash.ExitCode;reference=$reference.ExitCode;compact=$compact.ExitCode;report=$result}
    }
    & "$PSScriptRoot/build-installer.ps1" -PackageName $package -OutputBaseName $base -Compiler $Compiler
    if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
    $installer=Join-Path $project "dist/installers/$base.exe"
    $receipt=@{
      schema='aerotrace.release-verification.v1';version=$Version;bundleMode='review'
      installer=[IO.Path]::GetFileName($installer);sha256=(Get-FileHash -LiteralPath $installer).Hash
      bytes=(Get-Item -LiteralPath $installer).Length;package=$package;checks=$checks
      native="$reports/native.xml";capture="$run/performance.json"
      ddcFixture='PASS';physicalDdc='NOT RUN';physicalConversion='NOT RUN'
      installedUpgrade='NOT RUN';generativeAndEmbeddingAssets='ABSENT'
    }
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($installer+'.receipt.json') -Encoding UTF8
  } finally {
    $env:PATH=$previousPath
    $env:QT_QPA_PLATFORM=$previousPlatform
    $env:QT_SCALE_FACTOR=$previousScale
    $env:AEROTRACE_MANUAL_CHART_SOAK=$previousCharts
    $env:AEROTRACE_SOAK_SECONDS=$previousDuration
    $env:AEROTRACE_REFERENCE_COMPACT=$previousReferenceCompact
  }
} finally {
  Pop-Location
}
