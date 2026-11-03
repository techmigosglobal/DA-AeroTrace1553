param([string]$OutputDirectory=(Join-Path ([Environment]::GetFolderPath('MyDocuments')) ('AeroTrace-DDC-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& "$env:WINDIR\System32\pnputil.exe" /enum-drivers /class 'DDC Devices' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'driver-store.txt')
& "$env:WINDIR\System32\pnputil.exe" /enum-devices /class '{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}' /connected /drivers | Set-Content -LiteralPath (Join-Path $OutputDirectory 'connected-bindings.txt')
$root='HKLM:\SYSTEM\CurrentControlSet\Control\DDC1553Devices'
$logical=@();if(Test-Path -LiteralPath $root){$logical=@(Get-ChildItem -LiteralPath $root | ForEach-Object {$slotValues=Get-ItemProperty -LiteralPath $_.PSPath;if($null -eq $slotValues){[pscustomobject]@{PSChildName=$_.PSChildName;PSPath=$_.PSPath}}else{$slotValues}})}
$candidateAssignments=@();$emptySlots=@();$malformedSlots=@()
foreach($slot in $logical){
  $names=@($slot.PSObject.Properties.Name | Where-Object {$_ -notmatch '^PS'})
  if($names.Count -eq 0){$emptySlots+= $slot.PSChildName;continue}
  $missing=@('DeviceID','Channel','Family' | Where-Object {$_ -notin $names -or $null -eq $slot.$_ -or [string]::IsNullOrWhiteSpace([string]$slot.$_)})
  if($missing.Count){$malformedSlots+=@{logical=$slot.PSChildName;missing=$missing;values=$slot};continue}
  $candidateAssignments+=@{logical=$slot.PSChildName;values=$slot;state='candidate; SDK identity/access unverified'}
}
$services=@(Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -match '^(e2mausb|acexusb|acexpci|emapci|emaebrpci)$'} | Select-Object Name,State,StartMode,PathName)
$devices=@(Get-PnpDevice -PresentOnly | Where-Object {$_.Class -eq 'DDC Devices' -or $_.InstanceId -like 'USB\VID_4DDC&PID_5590*'} | Select-Object FriendlyName,InstanceId,Class,Status,Problem)
@{utc=(Get-Date).ToUniversalTime().ToString('o');e2maExpectedHardwareId='USB\VID_4DDC&PID_5590';logicalRegistry=$root;logicalRegistryExists=(Test-Path -LiteralPath $root);logicalAssignments=$candidateAssignments;emptyLogicalSlots=$emptySlots;malformedLogicalSlots=$malformedSlots;rawLogicalSlots=$logical;services=$services;devices=$devices;note='Read-only evidence. Staged packages, connected devices and populated assignment candidates are distinct. Registry types/channel base are not guessed. No device initialization or driver modification performed.'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ddc-evidence.json') -Encoding UTF8
Write-Output "DDC evidence: $OutputDirectory"
