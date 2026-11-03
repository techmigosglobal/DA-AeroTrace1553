param([string]$ReportDirectory=(Join-Path $PSScriptRoot '..\out\diagnostic-collector-test'))
$ErrorActionPreference='Stop'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('AeroTraceDiagnostics-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path "$fixture\data\Logs","$fixture\custom sessions","$fixture\app" | Out-Null
'{"event":"fixture"}' | Set-Content -LiteralPath "$fixture\data\Logs\application-fixture.jsonl"
'fixture' | Set-Content -LiteralPath "$fixture\custom sessions\sample.atd"
foreach($suffix in @('.events.jsonl','.checks.jsonl','.configuration.json','.icd.jsonl','.profile.json','.status.txt')){'fixture' | Set-Content -LiteralPath ("$fixture\custom sessions\sample.atd"+$suffix)}
'[General]',('recordFolder='+"$fixture\custom sessions") | Set-Content -LiteralPath "$fixture\data\preferences.ini"
& "$PSScriptRoot\collect-diagnostics.ps1" -ApplicationDirectory "$fixture\app" -DataDirectory "$fixture\data" -OutputDirectory "$fixture\bundle" -SkipDeviceInventory
if(!(Test-Path -LiteralPath "$fixture\bundle.zip")){throw 'No diagnostics ZIP'}
$result=Get-Content -LiteralPath "$fixture\bundle\system.json" -Raw | ConvertFrom-Json
if($result.collectionIssues.Count -ne 0 -or $result.copied.Count -ne 8){throw ('Wrong evidence count or collection issues: '+($result | ConvertTo-Json -Depth 8))}
if(Test-Path -LiteralPath "$fixture\bundle\captures\sample.atd\sample.atd"){throw 'Raw recording copied unexpectedly'}
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
@{passed=$true;copied=$result.copied.Count;customSessionDirectory=$true;checksAndConfiguration=$true;zip=$true;rawExcluded=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDirectory 'acceptance.json')
# The fixture has a fixed, checked system-temp prefix and contains only this test's files.
$resolved=[IO.Path]::GetFullPath($fixture)
if($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('AeroTraceDiagnostics-')){Remove-Item -LiteralPath $resolved -Recurse -Force}
Write-Output 'PASS: custom recording path, all metadata sidecars, journal, ZIP and bounded collection'
