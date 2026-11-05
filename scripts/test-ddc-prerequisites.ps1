$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'ddc-prerequisites.psm1') -Force
$passed=0
function Assert($Condition,[string]$Message){if(!$Condition){throw $Message};$script:passed++}
function State($Devices=@(),$Apps=@()) {return @{os_build=26100;x64=$true;devices=$Devices;staged=@(@{driver_ver='4.10.2.1'});card_manager='C:\DDC\ddccm.exe';running_vendor_apps=$Apps}}
$missing=@{instance_id='USB\TEST';hardware_ids=@('USB\VID_4DDC&PID_0100');driver_version='';problem_code=28;inf=''}
$working=@{instance_id='USB\TEST';hardware_ids=@('USB\VID_4DDC&PID_0100');driver_version='3.6.15.1';problem_code=0;inf='oem99.inf'}
$package=[pscustomobject]@{id='fixture-only';qualified=$true;architecture='x64';windows_min_build=17763;windows_max_build=99999;hardware_ids=@('USB\VID_4DDC&PID_0100');driver_version='4.10.2.1';inf='fixture.inf'}
$manifest=@{driver_packages=@($package)}
$verify={param($root,$package,$build) $true}
$calls=New-Object System.Collections.ArrayList
$run={param([string[]]$arguments) [void]$calls.Add($arguments);return @{exit_code=0;output='test-only'}}
$read={State @($working)}
$dir=Join-Path $env:TEMP ('aerotrace-prereq-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir | Out-Null
Set-Content -LiteralPath (Join-Path $dir 'fixture.inf') -Value '; test fixture; never installed'
try {
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State) -Manifest $manifest -Root $dir -Mode InstallMissing -Apply -VerifyPackage $verify -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'PreserveNoDevice' -and $calls.Count -eq 0) 'No-device must preserve staged packages without running commands'
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($working)) -Manifest $manifest -Root $dir -Mode InstallMissing -Apply -VerifyPackage $verify -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'PreserveWorking' -and $calls.Count -eq 0) 'Working driver must be retained'
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($missing)) -Manifest $manifest -Root $dir -Mode InstallMissing -VerifyPackage $verify -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'DryRun' -and $calls.Count -eq 0) 'Default must be dry run'
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($missing)) -Manifest $manifest -Root $dir -Mode InstallMissing -Apply -VerifyPackage {$false} -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'Blocked' -and $calls.Count -eq 0) 'Missing receipt/signature must block before driver store writes'
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($missing) @('RTProAnalyzer')) -Manifest $manifest -Root $dir -Mode Repair -Apply -VerifyPackage $verify -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'Blocked' -and $calls.Count -eq 0) 'Running vendor app must block driver changes'
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($missing)) -Manifest $manifest -Root $dir -Mode InstallMissing -Apply -VerifyPackage $verify -RunCommand $run -ReadInventory $read
  Assert ($r.action -eq 'Reinspect' -and $calls.Count -eq 1) 'One verified INF should be passed once'
  Assert (($calls[0] -join '|') -eq ('/add-driver|'+(Join-Path $dir 'fixture.inf')+'|/install')) 'Command must not force, reboot, install every package, or delete shared drivers'
  $calls.Clear()
  $reboot={param($arguments) [void]$calls.Add($arguments);@{exit_code=3010;output='test-only restart'}}
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($missing)) -Manifest $manifest -Root $dir -Mode InstallMissing -Apply -VerifyPackage $verify -RunCommand $reboot -ReadInventory $read
  Assert ($r.action -eq 'RestartRequired' -and $r.reboot_required) '3010 requires later restart/reinspection'
  $calls.Clear()
  $backupFail={param($arguments) [void]$calls.Add($arguments);@{exit_code=5;output='test-only backup denied'}}
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($working)) -Manifest $manifest -Root $dir -Mode Repair -Apply -VerifyPackage $verify -RunCommand $backupFail -ReadInventory $read -BackupDirectory $dir
  Assert ($r.action -eq 'Blocked' -and $calls.Count -eq 1 -and $calls[0][0] -eq '/export-driver') 'Failed recovery backup must prevent installation'
  $calls.Clear()
  $r=Invoke-DdcPrerequisiteOperation -Inventory (State @($working)) -Manifest $manifest -Root $dir -Mode Repair -Apply -VerifyPackage $verify -RunCommand $run -ReadInventory $read -BackupDirectory $dir
  Assert ($calls.Count -eq 2 -and $calls[0][0] -eq '/export-driver' -and $calls[1][0] -eq '/add-driver') 'Explicit repair must back up first'
  $unsupported=State @($missing);$unsupported.os_build=10000
  Assert ((Get-DdcPrerequisiteDecision $unsupported $manifest InstallMissing).action -eq 'Blocked') 'Unsupported OS must block'
  $wrongId=@{instance_id='USB\OTHER';hardware_ids=@('USB\VID_0000&PID_0000');driver_version='';problem_code=28;inf=''}
  Assert ((Get-DdcPrerequisiteDecision (State @($wrongId)) $manifest InstallMissing).action -eq 'Blocked') 'Hardware match must be exact'
  Assert (!(Test-DdcDriverPackage $dir $package 26100)) 'Real verifier must reject unsigned fixture without receipts'
  Write-Output "PASS: $passed prerequisite policy/operation boundary assertions; no real PnP operation performed"
} finally {
  $resolved=[IO.Path]::GetFullPath($dir)
  $prefix=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
  if($resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
