param([Parameter(Mandatory=$true)][string]$PackageRoot,[Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
New-Item -ItemType Directory -Force $ReportDirectory | Out-Null
$ReportDirectory=(Resolve-Path -LiteralPath $ReportDirectory).Path
$source=Join-Path $project 'out/build/desktop-local/apps/desktop/aerotrace_inference_tests.exe'
$test=Join-Path $PackageRoot 'qualification-checks.exe'
if(Test-Path -LiteralPath $test){throw 'Qualification executable already exists in the package'}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AssistantBudget {
  [StructLayout(LayoutKind.Sequential)] public struct Basic {
    public long ProcessTime, JobTime; public uint Flags;
    public UIntPtr MinimumWorkingSet, MaximumWorkingSet; public uint ActiveProcesses;
    public UIntPtr Affinity; public uint Priority, Scheduling;
  }
  [StructLayout(LayoutKind.Sequential)] public struct IO {
    public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes;
  }
  [StructLayout(LayoutKind.Sequential)] public struct Extended {
    public Basic Basic; public IO IO; public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
  }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr CreateJobObject(IntPtr attributes,string name);
  [DllImport("kernel32.dll",SetLastError=true)] public static extern bool SetInformationJobObject(IntPtr job,int info,ref Extended limits,uint size);
  [DllImport("kernel32.dll",SetLastError=true)] public static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$job=[AssistantBudget]::CreateJobObject([IntPtr]::Zero,$null)
if($job -eq [IntPtr]::Zero){throw 'Could not create qualification memory budget'}
$limits=New-Object AssistantBudget+Extended
$limits.Basic.Flags=0x2000 -bor 0x200
$limits.JobMemory=[UIntPtr]::new([uint64](8GB))
if(![AssistantBudget]::SetInformationJobObject($job,9,[ref]$limits,[Runtime.InteropServices.Marshal]::SizeOf($limits))){
  [void][AssistantBudget]::CloseHandle($job);throw 'Could not enforce the 8 GiB process-tree commit budget'
}
$savedPath=$env:PATH
$process=$null
try {
  Copy-Item -LiteralPath $source -Destination $test
  $env:PATH="$env:SystemRoot/System32;$env:SystemRoot"
  $timer=[Diagnostics.Stopwatch]::StartNew()
  $process=Start-Process -FilePath $test -ArgumentList @('--qualification','--report',('"'+(Join-Path $ReportDirectory 'qualification.txt')+'"')) -WindowStyle Hidden -WorkingDirectory $PackageRoot -PassThru -RedirectStandardOutput (Join-Path $ReportDirectory 'stdout.txt') -RedirectStandardError (Join-Path $ReportDirectory 'stderr.txt')
  if(![AssistantBudget]::AssignProcessToJobObject($job,$process.Handle)){throw 'Could not assign qualification process to its memory budget'}
  while(!$process.WaitForExit(1000)){}
  $process.Refresh()
  $hostMemory=Get-CimInstance Win32_OperatingSystem
  @{schema='aerotrace.assistant.qualification.v1';exitCode=$process.ExitCode;elapsedSeconds=$timer.Elapsed.TotalSeconds;
    processTreeCommitBudgetGiB=8;modelCommitLimitGiB=2.5;modelCpuCapPercent=20;modelThreads=2;
    hostPhysicalMemoryKiB=$hostMemory.TotalVisibleMemorySize;physicalEightGiBTarget='NOT RUN';
    qualification='Packaged runtime under an 8 GiB process-tree commit budget on the development host. This does not emulate OS memory pressure or establish physical 8 GB laptop latency.'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDirectory 'environment.json') -Encoding UTF8
  Get-Content -LiteralPath (Join-Path $ReportDirectory 'qualification.txt')
  if($process.ExitCode -ne 0){throw 'Packaged offline assistant qualification failed'}
} finally {
  $env:PATH=$savedPath
  [void][AssistantBudget]::CloseHandle($job)
  if($process -and !$process.HasExited){$process.WaitForExit(5000) | Out-Null}
  Remove-Item -LiteralPath $test -Force -ErrorAction SilentlyContinue
}
