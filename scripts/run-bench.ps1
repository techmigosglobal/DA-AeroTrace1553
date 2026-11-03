param([string]$KitRoot=$PSScriptRoot,[Parameter(Mandatory=$true)][string]$OutputDirectory,[Parameter(Mandatory=$true)][string]$Runtime,[ValidateRange(1,604800)][int]$Seconds=3600,[ValidateRange(1,1000)][int]$Cycles=1,[int]$Logical1=0,[int]$Logical2=1,[string]$ReferenceRecording='')
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Choose a new evidence directory'}
& (Join-Path $KitRoot 'aerotrace_provider_info.exe') $Runtime
if($LASTEXITCODE -ne 0){throw 'BLOCKED: SDK provider inspection failed; no card was opened'}
& (Join-Path $KitRoot 'aerotrace_qualification.exe') --output $OutputDirectory --seconds $Seconds --cycles $Cycles --streams '1A,1B,2A,2B' --logical1 $Logical1 --logical2 $Logical2 --runtime $Runtime --source-label 'physical-bench'
$captureExit=$LASTEXITCODE
if($ReferenceRecording){foreach($file in Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.atd'){
  & (Join-Path $KitRoot 'aerotrace_compare.exe') $ReferenceRecording $file.FullName ($file.FullName+'.reference.json')
  if($LASTEXITCODE -ne 0){throw "Reference comparison failed: $($file.Name)"}
}}
if($captureExit -ne 0){throw "Capture evidence incomplete (exit $captureExit); preserve the entire directory"}
Write-Output 'Capture evidence collected. Physical qualification remains NOT RUN until reference/timing/recovery checks and operator sign-off are completed.'
