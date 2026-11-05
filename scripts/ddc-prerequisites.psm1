Set-StrictMode -Version 2

function Get-DdcPrerequisiteDecision {
  param([Parameter(Mandatory=$true)]$Inventory,[Parameter(Mandatory=$true)]$Manifest,[ValidateSet('Inspect','InstallMissing','Repair')][string]$Mode='Inspect')
  $devices=@($Inventory.devices)
  if(!$Inventory.x64 -or [int]$Inventory.os_build -lt 17763){return @{action='Blocked';reason='Windows 10 1809 or later, x64, is required'}}
  # Do not stage even a newer package while no hardware is present: it can
  # change Windows selection on a future reconnect.
  if(!$devices.Count){return @{action='PreserveNoDevice';reason='No present DDC hardware; active and staged packages are unchanged'}}
  if($Mode -eq 'Inspect'){return @{action='InspectOnly';reason='Read-only prerequisite inspection'}}
  $missing=@($devices | Where-Object {![string]$_.driver_version -or [int]$_.problem_code -eq 28})
  if($Mode -eq 'InstallMissing' -and !$missing.Count){return @{action='PreserveWorking';reason='Existing driver is retained; repair is an explicit action'}}
  $targets=if($Mode -eq 'Repair'){$devices}else{$missing}
  foreach($package in @($Manifest.driver_packages)){
    if(!$package.qualified -or $package.architecture -ne 'x64' -or [int]$Inventory.os_build -lt [int]$package.windows_min_build -or [int]$Inventory.os_build -gt [int]$package.windows_max_build){continue}
    $matched=@($targets | Where-Object {$ids=@($_.hardware_ids);@($package.hardware_ids | Where-Object {$ids -contains $_}).Count -gt 0})
    if(!$matched.Count){continue}
    $otherWorking=@($devices | Where-Object {
      $device=$_; $ids=@($device.hardware_ids)
      [string]$device.driver_version -and [int]$device.problem_code -eq 0 -and
      @($package.hardware_ids | Where-Object {$ids -contains $_}).Count -gt 0 -and
      @($matched | Where-Object {$_.instance_id -eq $device.instance_id}).Count -eq 0
    })
    if($otherWorking.Count){return @{action='Blocked';reason='The INF could also update another working DDC device; no shared change is made'}}
    if(@($Inventory.running_vendor_apps).Count){return @{action='Blocked';reason='Close vendor hardware applications before a driver operation'}}
    return @{action=$Mode;reason='Exact OS and hardware candidate requires receipt/signature verification';package=$package;targets=@($matched | ForEach-Object {$_.instance_id})}
  }
  return @{action='Blocked';reason='No vendor-authorized, physically qualified package matches the present hardware and OS; existing drivers are unchanged'}
}

function Resolve-DdcManifestFile {
  param([string]$Root,[string]$Relative)
  if(!$Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or @($Relative.Replace('\','/').Split('/') | Where-Object {$_ -in @('','..','.')}).Count){throw 'Invalid prerequisite path'}
  $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
  $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
  if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf)){throw 'Missing or escaping prerequisite file'}
  $check=Get-Item -LiteralPath $path
  while($check -and $check.FullName.TrimEnd('\') -ne $prefix.TrimEnd('\')){
    if(($check.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Prerequisite reparse points are not permitted'}
    $check=if($check.PSIsContainer){$check.Parent}else{$check.Directory}
    if(!$check){break}
  }
  return $path
}

function Test-DdcDriverPackage {
  param([string]$Root,$Package,[int]$OsBuild)
  try {
    if(!$Package.rights_receipt -or !$Package.qualification_receipt -or !$Package.signature_receipt){return $false}
    $rights=Get-Content -LiteralPath (Resolve-DdcManifestFile $Root $Package.rights_receipt) -Raw | ConvertFrom-Json
    $qualification=Get-Content -LiteralPath (Resolve-DdcManifestFile $Root $Package.qualification_receipt) -Raw | ConvertFrom-Json
    $signature=Get-Content -LiteralPath (Resolve-DdcManifestFile $Root $Package.signature_receipt) -Raw | ConvertFrom-Json
    if($rights.schema -ne 'aerotrace.vendor-redistribution.v1' -or $rights.status -ne 'APPROVED' -or !$rights.issuer -or !$rights.authorization_reference -or $rights.package_id -ne $Package.id){return $false}
    if($qualification.schema -ne 'aerotrace.driver-qualification.v1' -or $qualification.status -ne 'PASS' -or $qualification.package_id -ne $Package.id -or $qualification.driver_version -ne $Package.driver_version -or !$qualification.rtpro_coexistence -or !$qualification.four_stream_capture -or !$qualification.reconnect -or !$qualification.evidence_reference){return $false}
    if($OsBuild -lt $qualification.windows_min_build -or $OsBuild -gt $qualification.windows_max_build){return $false}
    foreach($id in @($Package.hardware_ids)){if($id.Contains('*') -or $id.Contains('?') -or @($qualification.hardware_ids) -notcontains $id){return $false}}
    if($signature.schema -ne 'aerotrace.driver-signature-verification.v1' -or $signature.status -ne 'PASS' -or $signature.package_id -ne $Package.id -or !$signature.verifier -or !$signature.signer){return $false}
    $properties=@($Package.files.PSObject.Properties)
    if(!$properties.Count){return $false}
    $bound=@($rights.files.PSObject.Properties);$verified=@($signature.files.PSObject.Properties)
    if($bound.Count -ne $properties.Count -or $verified.Count -ne $properties.Count){return $false}
    foreach($entry in $properties){
      if($entry.Value -notmatch '^[a-fA-F0-9]{64}$'){return $false}
      $path=Resolve-DdcManifestFile $Root $entry.Name
      $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
      if($hash -ne $entry.Value -or $rights.files.($entry.Name) -ne $hash -or $signature.files.($entry.Name) -ne $hash){return $false}
    }
    $inf=Resolve-DdcManifestFile $Root $Package.inf
    $catalog=Resolve-DdcManifestFile $Root $Package.catalog
    if(@($properties.Name) -notcontains $Package.inf -or @($properties.Name) -notcontains $Package.catalog){return $false}
    $infText=Get-Content -LiteralPath $inf -Raw
    if($infText -notmatch ('(?im)^\s*DriverVer\s*=\s*[^,]+,\s*'+[regex]::Escape($Package.driver_version)+'\s*$')){return $false}
    foreach($id in @($Package.hardware_ids)){if($infText.IndexOf($id,[StringComparison]::OrdinalIgnoreCase) -lt 0){return $false}}
    $trust=Get-AuthenticodeSignature -LiteralPath $catalog
    return $trust.Status -eq 'Valid'
  } catch {return $false}
}

function Invoke-DdcPrerequisiteOperation {
  param($Inventory,$Manifest,[string]$Root,[ValidateSet('Inspect','InstallMissing','Repair')][string]$Mode='Inspect',[switch]$Apply,
    [scriptblock]$VerifyPackage={param($root,$package,$build) Test-DdcDriverPackage $root $package $build},
    [scriptblock]$RunCommand,[scriptblock]$ReadInventory,[string]$BackupDirectory='')
  $decision=Get-DdcPrerequisiteDecision $Inventory $Manifest $Mode
  $result=@{schema='aerotrace.ddc-prerequisite-result.v1';action=$decision.action;reason=$decision.reason;applied=$false;reboot_required=$false;before=$Inventory;commands=@();card_manager=if($Inventory.card_manager){'Reuse installed Card Manager'}else{'Missing; approved vendor component recipe required'}}
  if($decision.action -notin @('InstallMissing','Repair')){return $result}
  if(!(& $VerifyPackage $Root $decision.package ([int]$Inventory.os_build))){$result.action='Blocked';$result.reason='Vendor rights, physical qualification, signed catalog and exact file membership verification are required';return $result}
  $result.package_id=$decision.package.id
  if(!$Apply){$result.action='DryRun';$result.reason='Verified candidate; no driver command executed';return $result}
  if(!$RunCommand -or !$ReadInventory){throw 'Actual operations require explicit command and inventory providers'}
  if($Mode -eq 'Repair'){
    if(!$BackupDirectory){throw 'Repair requires a recovery receipt/backup directory'}
    $previous=@($Inventory.devices | Where-Object {$decision.targets -contains $_.instance_id})
    foreach($device in $previous){
      if($device.inf -and $device.inf -match '^oem[0-9]+\.inf$'){
        $export=& $RunCommand @('/export-driver',$device.inf,$BackupDirectory)
        $result.commands+=@{operation='backup';exit_code=$export.exit_code;output=$export.output}
        if($export.exit_code -ne 0){$result.action='Blocked';$result.reason='Prior shared driver backup failed; no installation attempted';return $result}
      }elseif($device.driver_version){$result.action='Blocked';$result.reason='Prior driver identity is unavailable for recovery; no repair attempted';return $result}
    }
  }
  $inf=Resolve-DdcManifestFile $Root $decision.package.inf
  $installed=& $RunCommand @('/add-driver',$inf,'/install')
  $result.commands+=@{operation='add-driver';exit_code=$installed.exit_code;output=$installed.output}
  $result.applied=$true
  $result.after=& $ReadInventory
  switch([int]$installed.exit_code){
    0 {$result.action='Reinspect';$result.reason='Command completed; re-enumerated state is evidence, not capture qualification'}
    259 {$result.action='NoMatchingChange';$result.reason='No matching device or a better driver already exists; capture readiness is not asserted'}
    3010 {$result.action='RestartRequired';$result.reboot_required=$true;$result.reason='Restart and re-enumerate before opening hardware'}
    1641 {$result.action='UnexpectedRestart';$result.reboot_required=$true;$result.reason='Restart was initiated externally; no automatic reboot was requested'}
    default {$result.action='Failed';$result.reason='Driver operation failed; prior package retained. Recovery is explicit, not an automatic shared-driver removal'}
  }
  return $result
}

Export-ModuleMember -Function Get-DdcPrerequisiteDecision,Invoke-DdcPrerequisiteOperation,Test-DdcDriverPackage,Resolve-DdcManifestFile
