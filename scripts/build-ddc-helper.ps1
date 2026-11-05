param(
  [string]$SdkRoot='C:/DDC/aceXtremeSDKv3.6.15',
  [string]$BuildDirectory='out/build/ddc-helper-x86-062',
  [string]$PackDestination='out/helper-packs-062/ddc-3.6.15-x86',
  [switch]$Test
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(![IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $project $BuildDirectory}
if(![IO.Path]::IsPathRooted($PackDestination)){$PackDestination=Join-Path $project $PackDestination}
$runtime=Join-Path $SdkRoot 'Lib/Win32/Release/emacepl.dll'
if(!(Test-Path -LiteralPath $runtime -PathType Leaf)){throw 'Matching licensed Win32 runtime is missing'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 C++ Build Tools are required'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$environmentScript=Join-Path $BuildDirectory 'init-msvc-x86.cmd'
[IO.File]::WriteAllLines($environmentScript,@('@echo off',('call "'+$vcvars+'" x64_x86 >nul'),'if errorlevel 1 exit /b 1','set'))
$lines=& $env:ComSpec /d /c call $environmentScript
if($LASTEXITCODE -ne 0){throw 'MSVC x86 environment initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
$env:PATH='C:\Qt\Tools\Ninja;'+$env:PATH
$cmake='C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake -S (Join-Path $project 'tools/ddc_helper') -B $BuildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_COMPILER=cl.exe' "-DDDC_SDK_ROOT=$SdkRoot"
if($LASTEXITCODE -ne 0){throw 'x86 helper configure failed'}
& $cmake --build $BuildDirectory --parallel 2
if($LASTEXITCODE -ne 0){throw 'x86 helper compilation failed'}
& (Join-Path $BuildDirectory 'aerotrace_helper_protocol_tests.exe')
if($LASTEXITCODE -ne 0){throw 'x86 helper wire contract failed'}
$helper=Join-Path $BuildDirectory 'aerotrace_ddc_helper_x86.exe'
$inspectionText=& $helper --inspect $runtime
if($LASTEXITCODE -ne 0){throw 'x86 runtime inspection failed; no card was opened'}
$inspection=$inspectionText | ConvertFrom-Json
$headerRows=New-Object System.Collections.Generic.List[string]
foreach($header in (Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'Include') -Recurse -File -Filter '*.h' | Sort-Object FullName)){$relative=$header.FullName.Substring($SdkRoot.TrimEnd('/','\').Length+1).Replace('\','/');$headerRows.Add($relative+':'+(Get-FileHash -LiteralPath $header.FullName -Algorithm SHA256).Hash.ToLowerInvariant())}
$algorithm=[Security.Cryptography.SHA256]::Create()
try{$headerHash=([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes(($headerRows -join "`n")+"`n")))).Replace('-','').ToLowerInvariant()}finally{$algorithm.Dispose()}
function Write-Pack([string]$Destination,[bool]$Fixture){
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  Copy-Item -LiteralPath $helper -Destination (Join-Path $Destination 'aerotrace_ddc_helper_x86.exe') -Force
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $Destination 'emacepl.dll') -Force
  $license=@((Join-Path $SdkRoot 'Documentation/license.txt'),(Join-Path $SdkRoot 'docs/license.txt')) | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
  if(!$license){throw 'SDK license text is unavailable'}
  Copy-Item -LiteralPath $license -Destination (Join-Path $Destination 'DDC-SDK-LICENSE.txt') -Force
  $modes=@('Auto');if($inspection.mti){$modes+='MTI'};if($inspection.legacy_mt){$modes+='LegacyMT'}
  $manifest=[ordered]@{schema='aerotrace.ddc-helper-pack.v1';id='ddc-3.6.15-x86';architecture='x86';protocol=1;fixture=$Fixture;helper=@{path='aerotrace_ddc_helper_x86.exe';sha256=(Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant()};runtime=@{path='emacepl.dll';sha256=$inspection.runtime_sha256;file_version=(Get-Item -LiteralPath $runtime).VersionInfo.FileVersion};sdk=@{identity=(Split-Path $SdkRoot -Leaf);header_sha256=$headerHash};receive_modes=$modes;receipts=@{compiled=$true;contract_tested=$true;runtime_inspected=$true};compatibility=@{matrix=@()};physical_qualification='NOT RUN'}
  $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Destination 'helper-pack.json') -Encoding UTF8
}
$fixturePack=Join-Path $BuildDirectory 'fixtures/ddc-3.6.15-x86'
Write-Pack $fixturePack $true
if(!$Test){throw 'Use -Test to complete fixture integration before producing a verified deployment pack'}
if($Test){
  & (Join-Path $BuildDirectory 'aerotrace_helper_integration_tests.exe') (Join-Path $fixturePack 'helper-pack.json')
  if($LASTEXITCODE -ne 0){throw 'x86 helper fixture integration failed'}
}
Write-Pack $PackDestination $false
$receipt=[ordered]@{schema='aerotrace.helper-software-verification.v1';status='PASS';compiled=$true;wire_contract=$true;fixture_integration=[bool]$Test;runtime_inspected=$true;helper_sha256=(Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant();runtime_sha256=$inspection.runtime_sha256;header_sha256=$headerHash;hardware_opened=$false;physical_qualification='NOT RUN';tested_utc=(Get-Date).ToUniversalTime().ToString('o')}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $PackDestination 'software-verification.json') -Encoding UTF8
Write-Output "Verified x86 helper pack: $PackDestination. Physical qualification NOT RUN."
