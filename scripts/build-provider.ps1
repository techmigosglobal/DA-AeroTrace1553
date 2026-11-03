param([Parameter(Mandatory=$true)][string]$SdkRoot,[Parameter(Mandatory=$true)][string]$ProviderId,[string]$BuildDirectory='',[string]$Destination='',[string]$SdkIdentity='',[string]$Runtime='')
$ErrorActionPreference='Stop'
if($ProviderId -notmatch '^[A-Za-z0-9._-]+$'){throw 'ProviderId may contain only letters, numbers, dot, underscore and dash'}
if($SdkIdentity -and $SdkIdentity -notmatch '^[A-Za-z0-9._+-]+$'){throw 'SdkIdentity may contain only letters, numbers, dot, underscore, plus and dash'}
if($Runtime -and !(Test-Path -LiteralPath $Runtime -PathType Leaf)){throw 'BLOCKED: explicit runtime DLL is unavailable'}
$project=Split-Path $PSScriptRoot -Parent
if(!(Test-Path -LiteralPath $SdkRoot -PathType Container)){throw "BLOCKED: licensed SDK folder unavailable: $SdkRoot"}
if(!$BuildDirectory){$BuildDirectory=Join-Path $project "out/build/provider-$ProviderId"}
$env:PATH='C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;'+$env:PATH
$cmake='C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake -S $project -B $BuildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DAEROTRACE_BUILD_GUI=OFF' '-DAEROTRACE_RELEASE_IPO=OFF' '-DAEROTRACE_ENABLE_DDC=ON' "-DDDC_SDK_ROOT=$SdkRoot" "-DAEROTRACE_DDC_PROVIDER_ID=$ProviderId" "-DAEROTRACE_DDC_SDK_IDENTITY=$SdkIdentity" "-DAEROTRACE_DDC_PROVIDER_RUNTIME=$Runtime"
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
if($Destination){New-Item -ItemType Directory -Force $Destination | Out-Null;Copy-Item -Path (Join-Path $BuildDirectory 'providers/*') -Destination $Destination}
Write-Output "Provider and descriptor: $BuildDirectory/providers. Physical qualification NOT RUN."
