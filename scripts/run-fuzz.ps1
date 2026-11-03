param([string]$BuildDirectory='out/build/backend-clang',[string]$OutputDirectory='out/fuzz',[ValidateRange(1,86400)][int]$Seconds=300)
$ErrorActionPreference='Stop';$project=Split-Path $PSScriptRoot -Parent
if(![IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $project $BuildDirectory}
if(![IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $project $OutputDirectory}
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$runtime=Join-Path $project 'out/toolchains/llvm-22.1.0/lib/clang/22/lib/windows';$env:PATH=$runtime+';'+(Join-Path $project 'out/toolchains/llvm-22.1.0/bin')+';'+$env:PATH
$all=@();$processes=@()
foreach($name in @('recording','protocol','packet')){
  $exe=Join-Path $BuildDirectory "tools/fuzz/aerotrace_fuzz_$name.exe"
  if(!(Test-Path -LiteralPath $exe)){throw "BLOCKED: fuzzer executable missing: $exe"}
  $folder=Join-Path $OutputDirectory $name;$corpus=Join-Path $folder 'corpus';New-Item -ItemType Directory -Force $corpus | Out-Null
  Copy-Item -Path (Join-Path $project "tools/fuzz/corpus/$name/*") -Destination $corpus -Force
  $env:ASAN_SYMBOLIZER_PATH=Join-Path $project 'out/toolchains/llvm-22.1.0/bin/llvm-symbolizer.exe'
  $prefix=$folder.Replace('\','/')+'/'
  $arguments=@(('"'+$corpus+'"'),"-max_total_time=$Seconds",'-timeout=10','-rss_limit_mb=1024','-max_len=65536',('-artifact_prefix="'+$prefix+'"'),'-print_final_stats=1')
  $proc=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $project -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'stderr.log')
  $proc.WaitForExit();$proc.Refresh()
  $processes+=@{name=$name;process=$proc;executable=$exe;folder=$folder;started=[DateTime]::UtcNow}
}
foreach($item in $processes){$item.process.WaitForExit();$item.process.Refresh();$log=Join-Path $item.folder 'stderr.log';$text=Get-Content -LiteralPath $log -Raw
  $status=if($item.process.ExitCode -eq 0 -and $text -match 'Done \d+ runs in'){ 'PASS' }else{'FAIL'}
  $all+=@{target=$item.name;status=$status;requestedSeconds=$Seconds;exitCode=$item.process.ExitCode;log=$log;executableSha256=(Get-FileHash -LiteralPath $item.executable -Algorithm SHA256).Hash}
}
@{schema='aerotrace.fuzz-receipt.v1';results=$all} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding UTF8
if(@($all | Where-Object status -ne 'PASS').Count){throw 'Fuzzing failed; preserve and minimize crash/timeout artifacts before rerunning'}
