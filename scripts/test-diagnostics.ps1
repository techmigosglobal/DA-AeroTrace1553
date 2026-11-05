param([string]$ReportDirectory=(Join-Path $PSScriptRoot '..\out\diagnostic-collector-test'))
$ErrorActionPreference='Stop'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('AeroTraceDiagnostics-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path "$fixture\data\Logs","$fixture\custom sessions","$fixture\app" | Out-Null
'{"event":"fixture"}' | Set-Content -LiteralPath "$fixture\data\Logs\application-fixture.jsonl"
'fixture' | Set-Content -LiteralPath "$fixture\custom sessions\sample.atd"
foreach($suffix in @('.events.jsonl','.checks.jsonl','.configuration.json','.icd.jsonl','.profile.json','.status.txt')){'fixture' | Set-Content -LiteralPath ("$fixture\custom sessions\sample.atd"+$suffix)}
'[General]',('recordFolder='+"$fixture\custom sessions") | Set-Content -LiteralPath "$fixture\data\preferences.ini"
'fixture private runtime' | Set-Content -LiteralPath "$fixture\data\private-runtime.dll"
@{schema='aerotrace.connection-evidence.v1';latest_attempt=@{runtime_path="$fixture\data\private-runtime.dll";backend='native_x64';attempt_id='fixture-attempt';error='registry failure';failure=@{native_code=-80}}} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$fixture\data\connection-latest.json"
& "$PSScriptRoot\collect-diagnostics.ps1" -ApplicationDirectory "$fixture\app" -DataDirectory "$fixture\data" -OutputDirectory "$fixture\bundle" -SkipDeviceInventory
if(!(Test-Path -LiteralPath "$fixture\bundle.zip")){throw 'No diagnostics ZIP'}
$result=Get-Content -LiteralPath "$fixture\bundle\system.json" -Raw | ConvertFrom-Json
if($result.collectionIssues.Count -ne 0 -or $result.copied.Count -ne 9){throw ('Wrong evidence count or collection issues: '+($result | ConvertTo-Json -Depth 8))}
if($result.runtime.path -ne "$fixture\data\private-runtime.dll" -or $result.runtime.selectionEvidence -ne 'recorded connection selection'){throw 'Collector did not retain the actual resolved private runtime'}
if(!(Test-Path -LiteralPath "$fixture\bundle\configuration\connection-latest.json")){throw 'Connection attempt evidence missing'}
if(Test-Path -LiteralPath "$fixture\bundle\captures\sample.atd\sample.atd"){throw 'Raw recording copied unexpectedly'}
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
@{passed=$true;copied=$result.copied.Count;customSessionDirectory=$true;checksAndConfiguration=$true;zip=$true;rawExcluded=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDirectory 'acceptance.json')
# The fixture has a fixed, checked system-temp prefix and contains only this test's files.
$resolved=[IO.Path]::GetFullPath($fixture)
if($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('AeroTraceDiagnostics-')){Remove-Item -LiteralPath $resolved -Recurse -Force}
Write-Output 'PASS: custom recording path, all metadata sidecars, journal, ZIP and bounded collection'
