param([Parameter(Mandatory=$true)][string]$SdkRoot,[Parameter(Mandatory=$true)][string]$ProviderId,[string]$BuildDirectory='',[string]$Destination='',[string]$SdkIdentity='',[string]$Runtime='',[string]$PackDestination='',[string]$SdkVersion='')
$ErrorActionPreference='Stop'
if($ProviderId -notmatch '^[A-Za-z0-9._-]+$'){throw 'ProviderId may contain only letters, numbers, dot, underscore and dash'}
if($SdkIdentity -and $SdkIdentity -notmatch '^[A-Za-z0-9._+-]+$'){throw 'SdkIdentity may contain only letters, numbers, dot, underscore, plus and dash'}
if($Runtime -and !(Test-Path -LiteralPath $Runtime -PathType Leaf)){throw 'BLOCKED: explicit runtime DLL is unavailable'}
$project=Split-Path $PSScriptRoot -Parent
if(!(Test-Path -LiteralPath $SdkRoot -PathType Container)){throw "BLOCKED: licensed SDK folder unavailable: $SdkRoot"}
if(!$BuildDirectory){$BuildDirectory=Join-Path $project "out/build/provider-$ProviderId"}
$env:PATH='C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;'+$env:PATH
$cmake='C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake -S $project -B $BuildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe' '-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe' '-DAEROTRACE_BUILD_GUI=OFF' '-DAEROTRACE_RELEASE_IPO=OFF' '-DAEROTRACE_ENABLE_DDC=ON' "-DDDC_SDK_ROOT=$SdkRoot" "-DAEROTRACE_DDC_PROVIDER_ID=$ProviderId" "-DAEROTRACE_DDC_SDK_IDENTITY=$SdkIdentity" "-DAEROTRACE_DDC_PROVIDER_RUNTIME=$Runtime"
if($LASTEXITCODE -ne 0){throw 'Provider configure failed'}
& $cmake --build $BuildDirectory --target aerotrace_ddc_provider aerotrace_ddc_tests aerotrace_provider_info --parallel 1
if($LASTEXITCODE -ne 0){throw 'Provider compilation failed against the supplied headers'}
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir $BuildDirectory -R '^ddc_adapter_contract$' --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Provider fixture contract failed'}
$previousProvider=$env:AEROTRACE_DDC_PROVIDER
try{
  $env:AEROTRACE_DDC_PROVIDER=Join-Path $BuildDirectory 'providers'
  $runtimeInput=if($Runtime){$Runtime}else{$SdkRoot}
  & (Join-Path $BuildDirectory 'aerotrace_provider_info.exe') $runtimeInput
  if($LASTEXITCODE -ne 0){throw 'Provider cannot inspect the selected vendor runtime; no card was opened'}
}finally{$env:AEROTRACE_DDC_PROVIDER=$previousProvider}
if($PackDestination){
  $descriptorPath=Join-Path $BuildDirectory "providers/$ProviderId.json"
  if(!(Test-Path -LiteralPath $descriptorPath)){throw 'Built provider descriptor is missing'}
  $descriptor=Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
  $providerPath=Join-Path (Join-Path $BuildDirectory 'providers') $descriptor.dll
  $runtimeCandidates=if($Runtime){@($Runtime)}else{@("$SdkRoot/libraries/emacepl/bin/x64/Release/emacepl.dll","$SdkRoot/Lib/x64/Release/emacepl.dll")}
  $runtimePath=$runtimeCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
  if(!$runtimePath){throw 'Exact inspected runtime is missing'}
  $licensePath=@("$SdkRoot/docs/license.txt","$SdkRoot/Documentation/license.txt") | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
  if(!$licensePath){throw 'SDK license text is unavailable'}
  $verificationPath=Join-Path $BuildDirectory 'provider-software-verification.json'
  @{schema='aerotrace.provider-software-verification.v1';status='PASS';compiled=$true;contract_tested=$true;runtime_inspected=$true;provider_sha256=(Get-FileHash -LiteralPath $providerPath -Algorithm SHA256).Hash.ToLowerInvariant();runtime_sha256=(Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant();contract='ddc_adapter_contract';tested_utc=(Get-Date).ToUniversalTime().ToString('o');physical_qualification='NOT RUN'} | ConvertTo-Json | Set-Content -LiteralPath $verificationPath -Encoding UTF8
  $runtimeVersion=(Get-Item -LiteralPath $runtimePath).VersionInfo.FileVersion
  if(!$SdkVersion){$SdkVersion=$descriptor.sdk}
  & python (Join-Path $PSScriptRoot 'provider-packs.py') create --descriptor $descriptorPath --provider $providerPath --runtime $runtimePath --license $licensePath --software-receipt $verificationPath --destination $PackDestination --sdk-version $SdkVersion --runtime-version $runtimeVersion
  if($LASTEXITCODE -ne 0){throw 'Private provider pack creation failed'}
}
if($Destination){New-Item -ItemType Directory -Force $Destination | Out-Null;Copy-Item -Path (Join-Path $BuildDirectory 'providers/*') -Destination $Destination}
Write-Output "Provider and descriptor: $BuildDirectory/providers. Physical qualification NOT RUN."
