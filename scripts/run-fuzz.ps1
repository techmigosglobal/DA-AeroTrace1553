param([string]$BuildDirectory='out/build/backend-clang',[string]$OutputDirectory='out/fuzz',[ValidateRange(1,86400)][int]$Seconds=300,[switch]$Parallel,[ValidateSet('recording','index','protocol','packet')][ValidateCount(1,4)][string[]]$Targets=@('recording','index','protocol','packet'))
$ErrorActionPreference='Stop';$project=Split-Path $PSScriptRoot -Parent
if(![IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $project $BuildDirectory}
if(![IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $project $OutputDirectory}
if(@($Targets | Select-Object -Unique).Count -ne $Targets.Count){throw 'Choose each fuzz target only once'}
if((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count){throw 'Choose a new or empty fuzz evidence directory; prior results are never overwritten'}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$sourceCommit=(& git -C $project rev-parse HEAD).Trim();$workingTreeDirty=![string]::IsNullOrWhiteSpace((& git -C $project status --porcelain | Out-String));$startedUtc=[DateTime]::UtcNow.ToString('o')
$runtime=Join-Path $project 'out/toolchains/llvm-22.1.0/lib/clang/22/lib/windows';$env:PATH=$runtime+';'+(Join-Path $project 'out/toolchains/llvm-22.1.0/bin')+';'+$env:PATH
$all=@();$processes=@()
foreach($name in $Targets){
  $exe=Join-Path $BuildDirectory "tools/fuzz/aerotrace_fuzz_$name.exe"
  if(!(Test-Path -LiteralPath $exe)){throw "BLOCKED: fuzzer executable missing: $exe"}
  $folder=Join-Path $OutputDirectory $name;$corpus=Join-Path $folder 'corpus';New-Item -ItemType Directory -Force $corpus | Out-Null
  $seedName=if($name -eq 'index'){'recording'}else{$name}
  Copy-Item -Path (Join-Path $project "tools/fuzz/corpus/$seedName/*") -Destination $corpus -Force
  $env:ASAN_SYMBOLIZER_PATH=Join-Path $project 'out/toolchains/llvm-22.1.0/bin/llvm-symbolizer.exe'
  $prefix=$folder.Replace('\','/')+'/'
  $arguments=@(('"'+$corpus+'"'),"-max_total_time=$Seconds",'-timeout=10','-rss_limit_mb=1024','-max_len=65536',('-artifact_prefix="'+$prefix+'"'),'-print_final_stats=1')
  $executableHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
  $proc=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $project -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'stderr.log')
  if(!$Parallel){$proc.WaitForExit();$proc.Refresh()}
  $processes+=@{name=$name;process=$proc;executableSha256=$executableHash;folder=$folder}
}
foreach($item in $processes){$item.process.WaitForExit();$item.process.Refresh();$log=Join-Path $item.folder 'stderr.log';$text=Get-Content -LiteralPath $log -Raw
  $status=if($item.process.ExitCode -eq 0 -and $text -match 'Done \d+ runs in'){ 'PASS' }else{'FAIL'}
  $all+=@{target=$item.name;status=$status;requestedSeconds=$Seconds;exitCode=$item.process.ExitCode;log=$log;executableSha256=$item.executableSha256}
}
@{schema='aerotrace.fuzz-receipt.v1';sourceCommit=$sourceCommit;workingTreeDirty=$workingTreeDirty;startedUtc=$startedUtc;completedUtc=[DateTime]::UtcNow.ToString('o');concurrentTargets=$(if($Parallel){$Targets.Count}else{1});results=$all} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding UTF8
if(@($all | Where-Object status -ne 'PASS').Count){throw 'Fuzzing failed; preserve and minimize crash/timeout artifacts before rerunning'}
