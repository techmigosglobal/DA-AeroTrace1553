param(
  [string]$ApplicationDirectory=(Split-Path $PSScriptRoot -Parent),
  [string]$OutputDirectory,
  [string]$DataDirectory=(Join-Path $env:LOCALAPPDATA 'AeroTrace\AeroTrace1553'),
  [string]$SessionDirectory,
  [string]$PreferencesPath,
  [string]$CapturePath,
  [string]$RuntimePath,
  [switch]$SkipDeviceInventory
)
$ErrorActionPreference='Stop'
$applicationRoot=[IO.Path]::GetFullPath($ApplicationDirectory)
if(!$OutputDirectory){$OutputDirectory=Join-Path ([Environment]::GetFolderPath('MyDocuments')) ('AeroTraceDiagnostics\'+(Get-Date -Format 'yyyyMMdd-HHmmssfff'))}
$bundleRoot=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $bundleRoot | Out-Null
$issues=New-Object System.Collections.Generic.List[string]
$copies=New-Object System.Collections.Generic.List[object]
$copiedBytes=0L
function Evidence-Sha256([string]$Path) {
  $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
  $algorithm=[Security.Cryptography.SHA256]::Create()
  try{return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','')}finally{$algorithm.Dispose();$stream.Dispose()}
}
function Copy-Evidence([string]$Source,[string]$Group) {
  if(!(Test-Path -LiteralPath $Source -PathType Leaf)){return}
  try {
    $item=Get-Item -LiteralPath $Source
    if($item.Length -gt 100MB -or $script:copiedBytes+$item.Length -gt 256MB){$script:issues.Add('Evidence size limit; preserved at original path: '+$Source);return}
    $targetDirectory=Join-Path $bundleRoot $Group
    New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    $target=Join-Path $targetDirectory $item.Name
    if([IO.Path]::GetFullPath($Source) -eq [IO.Path]::GetFullPath($target)){return}
    Copy-Item -LiteralPath $Source -Destination $target -Force
    $script:copiedBytes+=(Get-Item -LiteralPath $target).Length
    $script:copies.Add(@{source=$Source;copied=$target;sourceBytesAtSnapshot=$item.Length;sha256=(Evidence-Sha256 $target)})
  } catch {$script:issues.Add('Copy '+$Source+': '+$_.Exception.Message)}
}
if(!$PreferencesPath){$PreferencesPath=Join-Path $applicationRoot 'preferences.ini';if(!(Test-Path -LiteralPath $PreferencesPath)){$PreferencesPath=Join-Path $DataDirectory 'preferences.ini'}}
if(!$SessionDirectory){
  $SessionDirectory=Join-Path $DataDirectory 'Sessions'
  if(Test-Path -LiteralPath $PreferencesPath){$match=Get-Content -LiteralPath $PreferencesPath | Where-Object {$_ -match '^recordFolder='} | Select-Object -First 1;if($match){$SessionDirectory=$match.Substring(13).Replace('\\','\')}}
}
Copy-Evidence $PreferencesPath 'configuration'
Copy-Evidence (Join-Path $DataDirectory 'active-profile.json') 'configuration'
Copy-Evidence (Join-Path $applicationRoot 'deployment.json') 'configuration'
Copy-Evidence (Join-Path $applicationRoot 'SHA256SUMS.txt') 'configuration'
$logRoot=Join-Path $DataDirectory 'Logs'
if(Test-Path -LiteralPath $logRoot){Get-ChildItem -LiteralPath $logRoot -File | Sort-Object LastWriteTime -Descending | Select-Object -First 100 | ForEach-Object {Copy-Evidence $_.FullName 'logs'}}
$auditRoot=Join-Path $DataDirectory 'ReviewedActions'
if(Test-Path -LiteralPath $auditRoot){Get-ChildItem -LiteralPath $auditRoot -File | Sort-Object LastWriteTime -Descending | Select-Object -First 20 | ForEach-Object {Copy-Evidence $_.FullName 'reviewed-actions'}}
$captures=@()
if(Test-Path -LiteralPath $SessionDirectory){$captures=@(Get-ChildItem -LiteralPath $SessionDirectory -File | Where-Object {$_.Extension -in @('.atd','.atd2')} | Sort-Object LastWriteTime -Descending | Select-Object -First 10 | ForEach-Object {$_.FullName})}
if($CapturePath -and (Test-Path -LiteralPath (Split-Path $CapturePath -Parent))){$captures=@($CapturePath)+$captures}
foreach($capture in ($captures | Select-Object -Unique)){
  $group='captures\'+[IO.Path]::GetFileName($capture)
  foreach($suffix in @('.events.jsonl','.checks.jsonl','.configuration.json','.icd.jsonl','.profile.json','.status.txt','.session.json')){Copy-Evidence ($capture+$suffix) $group}
}
$present=@();$drivers=@()
if(!$SkipDeviceInventory){
  try{
    $registryRoot='HKLM:\SYSTEM\CurrentControlSet\Control\DDC1553Devices'
    $assignments=@()
    if(Test-Path -LiteralPath $registryRoot){$assignments=@(Get-ChildItem -LiteralPath $registryRoot | ForEach-Object {
      $key=$_;$item=Get-ItemProperty -LiteralPath $key.PSPath;$values=@{}
      foreach($property in $item.PSObject.Properties | Where-Object {$_.Name -notmatch '^PS'}){$values[$property.Name]=$property.Value}
      @{logical=$key.PSChildName;values=$values}
    })}
    @{registryPath=$registryRoot;exists=(Test-Path -LiteralPath $registryRoot);logicalAssignments=$assignments} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundleRoot 'ddc-logical-assignments.json') -Encoding UTF8
    & "$env:WINDIR\System32\pnputil.exe" /enum-devices /class '{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}' /connected /drivers | Set-Content -LiteralPath (Join-Path $bundleRoot 'ddc-connected-bindings.txt') -Encoding UTF8
  }catch{$issues.Add($_.Exception.Message)}

  try{$inventory=(& "$env:WINDIR\System32\pnputil.exe" /enum-drivers | Out-String);if($LASTEXITCODE -ne 0){throw 'Driver-store inventory failed'};@($inventory -split '(?:\r?\n){2,}' | Where-Object {$_ -match 'Data Device|\bDDC\b'}) | Set-Content -LiteralPath (Join-Path $bundleRoot 'ddc-driver-store.txt') -Encoding UTF8}catch{$issues.Add($_.Exception.Message)}
  try{$present=@(Get-PnpDevice -PresentOnly | Where-Object {$_.Class -eq 'DDC Devices' -or $_.FriendlyName -match 'DDC|Data Device|BU-(65|67)'} | Select-Object Class,FriendlyName,InstanceId,Status)}catch{$issues.Add($_.Exception.Message)}
  try{$drivers=@(Get-CimInstance Win32_PnPSignedDriver | Where-Object {$_.Manufacturer -match 'Data Device|^DDC' -or $_.DeviceName -match 'DDC|67103|67113'} | Select-Object DeviceName,Manufacturer,DriverVersion,DriverDate,IsSigned,InfName)}catch{$issues.Add($_.Exception.Message)}
}
if(!$RuntimePath){$RuntimePath=Join-Path $applicationRoot 'emacepl.dll';if(!(Test-Path -LiteralPath $RuntimePath)){$RuntimePath='C:\DDC\aceXtremeSDKv4.10.2\libraries\emacepl\bin\x64\Release\emacepl.dll'}}
$runtimeInfo=$null
if(Test-Path -LiteralPath $RuntimePath -PathType Leaf){$runtimeInfo=@{path=$RuntimePath;sha256=(Evidence-Sha256 $RuntimePath);fileVersion=(Get-Item -LiteralPath $RuntimePath).VersionInfo.FileVersion}}
$executable=Join-Path $applicationRoot 'AeroTrace1553.exe';$exeHash=$null;if(Test-Path -LiteralPath $executable){$exeHash=(Evidence-Sha256 $executable)}
$report=@{utc=(Get-Date).ToUniversalTime().ToString('o');application=$applicationRoot;executableSha256=$exeHash;dataDirectory=$DataDirectory;sessionDirectory=$SessionDirectory;capturePath=$CapturePath;os=[Environment]::OSVersion.VersionString;processArchitecture=$env:PROCESSOR_ARCHITECTURE;runtime=$runtimeInfo;presentDdcDevices=$present;ddcDrivers=$drivers;collectionIssues=$issues.ToArray();copied=$copies.ToArray();note='Read-only inventory. No bus initialization or driver/device reconfiguration. Evidence is a snapshot; files from an active capture can be incomplete. Raw ATD/Chapter 10 recordings, full PDFs and model files are not copied. Most recent 100 log files, 10 capture sidecar groups and 20 action audits, bounded to 256 MiB total.'}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundleRoot 'system.json') -Encoding UTF8
'Keep the complete recording (.atd) and all of its sidecars. This bundle omits raw ATD/Chapter 10 files and full PDFs. Stop capture before collecting for the most complete snapshot. See collectionIssues in system.json for omissions.' | Set-Content -LiteralPath (Join-Path $bundleRoot 'READ-ME.txt') -Encoding UTF8
try{Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $bundleRoot | ForEach-Object {$_.FullName}) -DestinationPath ($bundleRoot+'.zip') -Force}catch{$issues.Add('ZIP: '+$_.Exception.Message);$report.collectionIssues=$issues.ToArray();$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundleRoot 'system.json') -Encoding UTF8}
Write-Output "Diagnostics: $bundleRoot"
Write-Output "ZIP: $bundleRoot.zip"
