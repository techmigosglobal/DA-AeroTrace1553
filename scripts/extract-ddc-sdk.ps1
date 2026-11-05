param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Destination,[Parameter(Mandatory=$true)][string]$OverlayExtractor,[Parameter(Mandatory=$true)][string]$Unshield)
$ErrorActionPreference='Stop'
# This workflow only reads ZIP/InstallShield/CAB formats. It never launches
# setup.exe, changes SDK/driver registration, or copies into Windows/DDC folders.
foreach($path in @($Archive,$OverlayExtractor,$Unshield)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Required intake file missing: $path"}}
$Destination=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $Destination){throw 'Use a fresh extraction directory'}
$work=Join-Path $Destination 'archive'
New-Item -ItemType Directory -Path $work -Force | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $work
$setups=@(Get-ChildItem -LiteralPath $work -File -Filter '*_setup.exe' -Recurse)
if($setups.Count -ne 1){throw 'Archive must contain exactly one identifiable SDK setup payload'}
$payload=Join-Path $Destination 'payload'
& $OverlayExtractor --quiet $setups[0].FullName $payload
if($LASTEXITCODE -ne 0){throw 'InstallShield overlay extraction failed'}
$cab=Join-Path $payload 'Disk1/data1.cab'
if(!(Test-Path -LiteralPath $cab)){throw 'Expected InstallShield cabinet is missing'}
$sdkOutput=Join-Path $Destination 'files'
& $Unshield -d $sdkOutput x $cab *> (Join-Path $Destination 'cabinet-extraction.log')
if($LASTEXITCODE -ne 0){throw 'InstallShield cabinet extraction failed'}
$sdkRoot=Join-Path $sdkOutput 'SDK'
if(!(Test-Path -LiteralPath (Join-Path $sdkRoot 'libraries/emacepl/src/stdemace.h')) -or !(Test-Path -LiteralPath (Join-Path $sdkRoot 'libraries/emacepl/bin/x64/Release/emacepl.dll'))){throw 'Extraction did not recover usable SDK headers and x64 runtime; do not treat a successful extractor exit as sufficient'}
@{schema='aerotrace.sdk-extraction.v1';archive=(Resolve-Path -LiteralPath $Archive).Path;archive_sha256=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash;setup_sha256=(Get-FileHash -LiteralPath $setups[0].FullName -Algorithm SHA256).Hash;overlay_extractor_sha256=(Get-FileHash -LiteralPath $OverlayExtractor -Algorithm SHA256).Hash;cabinet_extractor_sha256=(Get-FileHash -LiteralPath $Unshield -Algorithm SHA256).Hash;sdk_root=$sdkRoot;vendor_installer_executed=$false;extracted_utc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'extraction-receipt.json') -Encoding UTF8
Write-Output "Extracted local SDK inputs: $sdkRoot. Drivers and installed applications unchanged."
