param([string]$Sdk3615='C:\DDC\aceXtremeSDKv3.6.15',[string]$Sdk495='out/sdk-inputs/ace-4.9.5-verified/SDK',[string]$Sdk4102='out/sdk-inputs/ace-4.10.2-verified/SDK',[string]$Destination='out/provider-packs-062')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
foreach($spec in @(@{id='ddc-3.6.15';root=$Sdk3615;version='3.6.15.1'},@{id='ddc-4.9.5';root=$Sdk495;version='4.9.5.1'},@{id='ddc-4.10.2';root=$Sdk4102;version='4.10.2.1'})){
  $sdkRoot=if([IO.Path]::IsPathRooted($spec.root)){$spec.root}else{Join-Path $project $spec.root}
  $packRoot=if([IO.Path]::IsPathRooted($Destination)){$Destination}else{Join-Path $project $Destination}
  & (Join-Path $PSScriptRoot 'build-provider.ps1') -SdkRoot $sdkRoot -ProviderId $spec.id -SdkIdentity ('aceXtremeSDK-'+$spec.version) -SdkVersion $spec.version -BuildDirectory (Join-Path $project ('out/build/provider-062-'+$spec.id)) -PackDestination (Join-Path $packRoot $spec.id)
  if($LASTEXITCODE -ne 0){throw "Provider pack verification failed: $($spec.id)"}
}
