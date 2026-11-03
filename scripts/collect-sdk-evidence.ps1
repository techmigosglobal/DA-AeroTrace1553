param([Parameter(Mandatory=$true)][string]$SdkRoot,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputFile){throw 'Choose a new evidence file'}
if(!(Test-Path -LiteralPath $SdkRoot -PathType Container)){throw 'BLOCKED: SDK folder unavailable'}
$root=(Resolve-Path -LiteralPath $SdkRoot).Path
$runtimes=@('Lib/x64/Release/emacepl.dll','libraries/emacepl/bin/x64/Release/emacepl.dll','emacepl.dll') | ForEach-Object {Join-Path $root $_} | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf}
$runtimeEvidence=@($runtimes | ForEach-Object {$f=Get-Item -LiteralPath $_;@{path=$f.FullName;fileVersion=$f.VersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$headers=@();foreach($folder in @('Include','libraries/emacepl/src','drivers/acex/src')){
  $path=Join-Path $root $folder
  if(Test-Path -LiteralPath $path){$headers+=@(Get-ChildItem -LiteralPath $path -Recurse -File -Filter '*.h' | ForEach-Object {@{path=$_.FullName.Substring($root.Length).TrimStart('\');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})}
}
$devices=@();$deviceStatus='PASS'
try{$devices=@(Get-CimInstance Win32_PnPSignedDriver -ErrorAction Stop | Where-Object {$_.DeviceID -match 'VID_4DDC' -or $_.Manufacturer -match 'Data Device Corporation' -or $_.DeviceName -match 'aceXtreme|1553|Enhanced Mini-ACE'} | Select-Object DeviceName,DeviceID,DriverVersion,DriverDate,InfName,Manufacturer)}catch{$deviceStatus='BLOCKED: '+$_.Exception.Message}
$status=if(!$runtimeEvidence.Count -or !$headers.Count){'BLOCKED'}else{'PASS'}
@{schema='aerotrace.sdk-evidence.v1';status=$status;collectedUtc=[DateTime]::UtcNow.ToString('o');sdkRoot=$root;os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;runtimes=$runtimeEvidence;headers=$headers;driverInventoryStatus=$deviceStatus;drivers=$devices;physicalQualification='NOT RUN';note='Package inventory only. No card was initialized and no driver was installed or modified.'} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputFile -Encoding UTF8
if($status -ne 'PASS'){throw 'BLOCKED: complete SDK headers/runtime evidence not found'}
