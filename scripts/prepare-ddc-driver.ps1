param([Parameter(Mandatory=$true)][string]$SourceDirectory,[Parameter(Mandatory=$true)][string]$Inf,[Parameter(Mandatory=$true)][string]$Catalog,[Parameter(Mandatory=$true)][string]$PackageId,[Parameter(Mandatory=$true)][string]$SignTool,[Parameter(Mandatory=$true)][string]$RightsReceipt,[Parameter(Mandatory=$true)][string]$QualificationReceipt,[Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
if($PackageId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]+$'){throw 'Invalid package ID'}
foreach($file in @($SignTool,$RightsReceipt,$QualificationReceipt)){if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw 'Actual verifier, authorization and qualification inputs are required'}}
if(Test-Path -LiteralPath $Destination){throw 'Use a fresh intake destination'}
Import-Module (Join-Path $PSScriptRoot 'ddc-prerequisites.psm1') -Force
$rights=Get-Content -LiteralPath $RightsReceipt -Raw | ConvertFrom-Json
$qualified=Get-Content -LiteralPath $QualificationReceipt -Raw | ConvertFrom-Json
if($rights.schema -ne 'aerotrace.vendor-redistribution.v1' -or $rights.status -ne 'APPROVED' -or $rights.package_id -ne $PackageId -or !$rights.issuer -or !$rights.authorization_reference){throw 'Vendor authorization receipt is not valid for this package'}
if($qualified.schema -ne 'aerotrace.driver-qualification.v1' -or $qualified.status -ne 'PASS' -or $qualified.package_id -ne $PackageId -or !$qualified.rtpro_coexistence -or !$qualified.four_stream_capture -or !$qualified.reconnect -or !$qualified.evidence_reference){throw 'Actual coexistence/capture/reconnect qualification evidence required'}
$source=[IO.Path]::GetFullPath($SourceDirectory)
$sourceFiles=@(Get-ChildItem -LiteralPath $source -File -Recurse)
if(!$sourceFiles.Count){throw 'Empty driver package'}
$files=@{}
$prefix=$source.TrimEnd('\')+'\'
foreach($file in $sourceFiles){
  if(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Reparse point in driver input'}
  $relative=$file.FullName.Substring($prefix.Length).Replace('\','/')
  $files[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$infPath=Resolve-DdcManifestFile $source $Inf
$catPath=Resolve-DdcManifestFile $source $Catalog
$trust=Get-AuthenticodeSignature -LiteralPath $catPath
if($trust.Status -ne 'Valid'){throw 'Vendor catalog trust verification failed'}
$verification=@()
& $SignTool verify /kp /v $catPath 2>&1 | ForEach-Object {$verification+=[string]$_}
if($LASTEXITCODE -ne 0){throw 'Kernel policy catalog signature verification failed'}
foreach($relative in @($files.Keys | Sort-Object)){
  if($relative -eq $Catalog){continue}
  & $SignTool verify /kp /v /c $catPath (Resolve-DdcManifestFile $source $relative) 2>&1 | ForEach-Object {$verification+=[string]$_}
  if($LASTEXITCODE -ne 0){throw "Catalog membership verification failed for $relative"}
}
# The external permission receipt binds exact bytes; no generic approval or
# placeholder text is converted into redistribution authority.
$expected=@($rights.files.PSObject.Properties)
if($expected.Count -ne $files.Count){throw 'Authorization receipt does not bind every package file'}
foreach($entry in $expected){if(!$files.ContainsKey($entry.Name) -or $files[$entry.Name] -ne $entry.Value){throw 'Authorization receipt does not bind these package bytes'}}
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach($relative in $files.Keys){
  $target=Join-Path $Destination $relative
  New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
  Copy-Item -LiteralPath (Resolve-DdcManifestFile $source $relative) -Destination $target
}
Copy-Item -LiteralPath $RightsReceipt -Destination (Join-Path $Destination 'redistribution.json')
Copy-Item -LiteralPath $QualificationReceipt -Destination (Join-Path $Destination 'qualification.json')
@{schema='aerotrace.driver-signature-verification.v1';status='PASS';package_id=$PackageId;files=$files;signer=$trust.SignerCertificate.Subject;verifier=(Resolve-Path -LiteralPath $SignTool).Path;verifier_sha256=(Get-FileHash -LiteralPath $SignTool -Algorithm SHA256).Hash;verified_utc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Destination 'signature-verification.json') -Encoding UTF8
$verification | Set-Content -LiteralPath (Join-Path $Destination 'signature-verification.log') -Encoding UTF8
Write-Output "Verified driver intake: $Destination. No PnP operation or vendor installer was executed."
