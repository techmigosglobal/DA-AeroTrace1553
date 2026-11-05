param([ValidateSet('Inspect','InstallMissing','Repair')][string]$Mode='Inspect',[switch]$Apply,[string]$Manifest='', [string]$ReceiptDirectory='')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'ddc-prerequisites.psm1') -Force
if(!$Manifest){$Manifest=Join-Path (Split-Path $PSScriptRoot -Parent) 'ddc-prerequisites.json'}
if(!$ReceiptDirectory){$ReceiptDirectory=Join-Path $env:LOCALAPPDATA ('AeroTrace1553/Diagnostics/prerequisites-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))}
if($Apply -and $Mode -eq 'Inspect'){throw 'Inspect never applies driver changes'}
if($Apply -and !([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Driver operations require administrator rights'}
function Get-Inventory {
  $devices=@();$class='{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}'
  $signed=@(Get-CimInstance Win32_PnPSignedDriver -ErrorAction Stop | Where-Object {$_.ClassGuid -eq $class -or $_.DeviceID -match '^(USB\\VID_4DDC|PCI\\VEN_4DDC)'})
  # A clean PC's DDC hardware may still have Unknown class/code28. Match the
  # vendor hardware prefix before expecting an installed device setup class.
  $present=@(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {$_.Class -eq 'DDC Devices' -or $_.InstanceId -match '^(USB\\VID_4DDC|PCI\\VEN_4DDC)'})
  if($present.Count -gt 128){throw 'Unexpected DDC device count; refusing a partial prerequisite inventory'}
  foreach($device in $present){
    $driver=$signed | Where-Object {$_.DeviceID -eq $device.InstanceId} | Select-Object -First 1
    $ids=@((Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop).Data)
    $problem=(Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_ProblemCode' -ErrorAction Stop).Data
    $devices+=@{instance_id=$device.InstanceId;hardware_ids=$ids;driver_version=if($driver){$driver.DriverVersion}else{''};problem_code=[int]$problem;inf=if($driver){$driver.InfName}else{''}}
  }
  $staged=@()
  foreach($prefix in @('acex*','e2ma*','emapci*','emaebrpci*')){
    foreach($dir in @(Get-ChildItem -LiteralPath "$env:windir/System32/DriverStore/FileRepository" -Directory -Filter $prefix -ErrorAction Stop)){
      foreach($inf in @(Get-ChildItem -LiteralPath $dir.FullName -Filter '*.inf' -File)){
        $version=Select-String -LiteralPath $inf.FullName -Pattern '^\s*DriverVer\s*=' | Select-Object -First 1
        $staged+=@{path=$inf.FullName;driver_ver=if($version){$version.Line.Trim()}else{''}}
      }
    }
  }
  $manager=@('C:\DDC\ddccm.exe',"$env:ProgramFiles/DDC/ddccm.exe",(Join-Path (Split-Path $PSScriptRoot -Parent) 'vendor/ddc/card-manager/ddccm.exe')) | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
  return @{os_build=[Environment]::OSVersion.Version.Build;x64=[Environment]::Is64BitOperatingSystem;devices=$devices;staged=$staged;card_manager=$manager;running_vendor_apps=@(Get-Process -ErrorAction Stop | Where-Object {$_.ProcessName -match '^RTProAnalyzer|^ddccm$'} | ForEach-Object {$_.ProcessName})}
}
$configuration=Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if($configuration.schema -ne 'aerotrace.ddc-prerequisites.v1'){throw 'Unknown prerequisite manifest schema'}
$inventory=Get-Inventory
$root=Split-Path ([IO.Path]::GetFullPath($Manifest)) -Parent
New-Item -ItemType Directory -Path $ReceiptDirectory -Force | Out-Null
$inventory | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $ReceiptDirectory 'before.json') -Encoding UTF8
$run={param([string[]]$arguments)
  $lines=& "$env:windir/System32/pnputil.exe" @arguments 2>&1
  return @{exit_code=$LASTEXITCODE;output=($lines -join "`n")}
}
$result=Invoke-DdcPrerequisiteOperation -Inventory $inventory -Manifest $configuration -Root $root -Mode $Mode -Apply:$Apply -RunCommand $run -ReadInventory {Get-Inventory} -BackupDirectory $ReceiptDirectory
$result | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $ReceiptDirectory 'result.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 16
if($result.action -in @('Blocked','Failed','UnexpectedRestart')){exit 2}
if($result.reboot_required){exit 3010}
