param(
  [ValidateSet('Quick','Full')][string]$Profile='Quick',
  [string]$BuildDirectory='out/build/desktop-local',
  [string]$BackendBuild='out/build/backend-clang',
  [string]$ReportDirectory='',
  [string]$QtRoot='C:/Qt/6.12.0/mingw_64',
  [switch]$SkipBuild,
  [switch]$ParallelFuzz,
  [ValidateRange(3,86400)][int]$SoakSeconds=600,
  [ValidateRange(1,86400)][int]$FuzzSeconds=300
)
$ErrorActionPreference='Stop';$project=Split-Path $PSScriptRoot -Parent
foreach($name in @('BuildDirectory','BackendBuild')){if(![IO.Path]::IsPathRooted((Get-Variable $name -ValueOnly))){Set-Variable $name (Join-Path $project (Get-Variable $name -ValueOnly))}}
if(!$ReportDirectory){$ReportDirectory=Join-Path $project ('out/preflight/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
if((Test-Path -LiteralPath $ReportDirectory) -and @(Get-ChildItem -LiteralPath $ReportDirectory -Force).Count){throw 'Report directory must be new or empty'}
New-Item -ItemType Directory -Force $ReportDirectory | Out-Null;$ReportDirectory=(Resolve-Path -LiteralPath $ReportDirectory).Path
$env:PATH='C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;'+(Join-Path $QtRoot 'bin')+';'+$env:PATH
$env:QT_QPA_PLATFORM='offscreen';$results=[Collections.Generic.List[object]]::new();$completed=$false
$commit=(& git -C $project rev-parse HEAD).Trim();$dirty=![string]::IsNullOrWhiteSpace((& git -C $project status --porcelain | Out-String))
function Write-Receipt {
  $state=if(!$completed){'RUNNING'}elseif(@($results | Where-Object status -eq 'FAIL').Count){'FAIL'}elseif(@($results | Where-Object status -eq 'BLOCKED').Count){'BLOCKED'}else{'PASS'}
  $receipt=@{schema='aerotrace.preflight.v1';profile=$Profile;status=$state;completed=$completed;commit=$commit;workingTreeDirty=$dirty;desktopBuild=$BuildDirectory;backendBuild=$BackendBuild;reusedDesktopBuild=[bool]$SkipBuild;parallelFuzz=[bool]$ParallelFuzz;startedUtc=$started;updatedUtc=[DateTime]::UtcNow.ToString('o');physicalQualification='NOT RUN';ciCd='NOT CONFIGURED / NOT RUN';results=$results.ToArray()}
  $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $ReportDirectory 'receipt.json') -Encoding UTF8
  $rows=@('# Local preflight',"","Profile: $Profile; commit: $commit","",'Physical qualification: NOT RUN. No CI/CD workflows or remote operations.','', '| Check | Status | Evidence |','|---|---|---|')
  foreach($r in $results){$rows+='| '+$r.name+' | '+$r.status+' | '+$r.evidence+' |'}
  $rows | Set-Content -LiteralPath (Join-Path $ReportDirectory 'summary.md') -Encoding UTF8
}
function Check([string]$Name,[scriptblock]$Action){
  $log=Join-Path $ReportDirectory ($Name+'.log');$timer=[Diagnostics.Stopwatch]::StartNew();$status='PASS';$detail=''
  try{$global:LASTEXITCODE=0;& $Action *>&1 | Out-File -LiteralPath $log -Encoding UTF8;if($LASTEXITCODE -ne 0){throw "Command exit code $LASTEXITCODE"}}
  catch{$detail=$_.Exception.Message;$status=if($detail -match 'BLOCKED:'){'BLOCKED'}else{'FAIL'};Add-Content -LiteralPath $log -Value $detail}
  $results.Add(@{name=$Name;status=$status;elapsedSeconds=$timer.Elapsed.TotalSeconds;evidence=$log;detail=$detail});Write-Receipt;Write-Output "$Name : $status"
}
function Run-Stress([int]$Seconds,[string]$Folder,[bool]$Contention){
  New-Item -ItemType Directory -Force $Folder | Out-Null;$env:AEROTRACE_SOAK_SECONDS=[string]$Seconds;$env:AEROTRACE_KEEP_SOAK='1';$env:AEROTRACE_SOAK_ROOT=Join-Path $ReportDirectory 'captures'
  New-Item -ItemType Directory -Force $env:AEROTRACE_SOAK_ROOT | Out-Null
  if($Contention){$env:AEROTRACE_SOAK_CONTENTION='1'}else{Remove-Item Env:AEROTRACE_SOAK_CONTENTION -ErrorAction SilentlyContinue}
  Remove-Item Env:AEROTRACE_DDC_PROVIDER -ErrorAction SilentlyContinue
  $exe=Join-Path $BuildDirectory 'apps/desktop/aerotrace_desktop.exe'
  @{schema='aerotrace.stress-launch.v1';sourceCommit=$commit;workingTreeDirty=$dirty;executable=$exe;executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;requestedSeconds=$Seconds;documentContention=$Contention;startedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Folder 'launch.json') -Encoding UTF8
  $proc=Start-Process -FilePath $exe -ArgumentList @('--performance-test',('"'+$Folder+'"')) -WorkingDirectory $project -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $Folder 'stdout.log') -RedirectStandardError (Join-Path $Folder 'stderr.log')
  if($proc.ExitCode -ne 0){throw "Stress process failed with exit $($proc.ExitCode)"}
  $report=Get-Content -LiteralPath (Join-Path $Folder 'performance.json') -Raw | ConvertFrom-Json
  if(!$report.passed){throw 'Stress acceptance failed'}
  if($Seconds -ge 65 -and !$report.memory_gate_measured){throw 'Stress memory gate was not measured'}
  if($Seconds -ge 30 -and (!$report.exact_csv_contents -or !$report.indexed_replay_contents)){throw 'Stress export/replay contents were not verified'}
}
$started=[DateTime]::UtcNow.ToString('o')
Check 'tool-inventory' {& git --version;& "$QtRoot/bin/qmake.exe" -query QT_VERSION;& 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' --version;if(!(Test-Path -LiteralPath "$QtRoot/lib/cmake/Qt6Test/Qt6TestConfig.cmake")){throw 'BLOCKED: Qt Test kit missing'}}
if(!$SkipBuild){Check 'desktop-build' {& "$PSScriptRoot/build.ps1" -QtRoot $QtRoot -BuildDirectory $BuildDirectory}}
Check 'mandatory-test-inventory' {
  $testJson=& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir $BuildDirectory --show-only=json-v1
  if($LASTEXITCODE -ne 0){throw 'Test discovery failed'}
  $names=@(($testJson | ConvertFrom-Json).tests | ForEach-Object name)
  foreach($required in @('offline_pdf_ocr_and_retrieval','gui_pdf_to_live_conversion','gui_demo_icd_live','qt_model_consistency','ddc_adapter_contract','provider_runtime_inspection')){if($required -notin $names){throw "BLOCKED: mandatory test was not configured: $required"}}
}
Check 'desktop-regressions' {& 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir $BuildDirectory --output-on-failure --output-junit (Join-Path $ReportDirectory 'desktop-tests.xml')}
if($Profile -eq 'Full'){
  Check 'native-sanitizer-build-tests' {& "$PSScriptRoot/build-verified-backend.ps1" -BuildDirectory $BackendBuild -Test}
  Check 'parser-fuzzing' {& "$PSScriptRoot/run-fuzz.ps1" -BuildDirectory $BackendBuild -OutputDirectory (Join-Path $ReportDirectory 'fuzz') -Seconds $FuzzSeconds -Parallel:$ParallelFuzz}
  # Do not run CPU-heavy fuzzers while collecting baseline GUI timing.
  Check 'four-stream-soak' {Run-Stress $SoakSeconds (Join-Path $ReportDirectory 'soak') $false}
  Check 'document-contention' {Run-Stress 30 (Join-Path $ReportDirectory 'contention') $true}
}
Check 'sdk-3.6.15' {
  if(!$env:AEROTRACE_SDK_3615_ROOT){throw 'BLOCKED: licensed SDK 3.6.15 headers/runtime and target driver evidence are unavailable. Set AEROTRACE_SDK_3615_ROOT to build its provider.'}
  & "$PSScriptRoot/build-provider.ps1" -SdkRoot $env:AEROTRACE_SDK_3615_ROOT -ProviderId 'ddc-3.6.15'
}
$completed=$true;Write-Receipt
if(@($results | Where-Object status -eq 'FAIL').Count){exit 1}
if(@($results | Where-Object status -eq 'BLOCKED').Count){exit 2}
exit 0
