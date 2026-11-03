param([Parameter(Mandatory=$true)][string]$BuildDirectory,[Parameter(Mandatory=$true)][string]$Destination,[string]$CompilerRoot='C:/Qt/Tools/mingw1310_64')
$ErrorActionPreference='Stop';$project=Split-Path $PSScriptRoot -Parent
if(Test-Path -LiteralPath $Destination){throw 'Choose a new bench-kit directory; existing evidence is never overwritten'}
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach($name in @('aerotrace_qualification.exe','aerotrace_provider_info.exe','aerotrace_compare.exe')){Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination $Destination}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'providers') -Destination $Destination -Recurse
foreach($name in @('libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll')){Copy-Item -LiteralPath (Join-Path $CompilerRoot "bin/$name") -Destination $Destination}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'run-bench.ps1') -Destination $Destination
Copy-Item -LiteralPath (Join-Path $project 'docs/SES-Bench-Checklist.md') -Destination $Destination
Copy-Item -LiteralPath (Join-Path $project 'docs/provider-contract.md') -Destination $Destination
$prefix=[IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\'
$files=Get-ChildItem -LiteralPath $Destination -File -Recurse | ForEach-Object {@{path=$_.FullName.Substring($prefix.Length);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}
@{schema='aerotrace.bench-kit.v1';physicalQualification='NOT RUN';vendorSdkIncluded=$false;compiler='MinGW x64';files=@($files)} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Destination 'kit-manifest.json') -Encoding UTF8
Write-Output "Bench kit: $Destination. Use the bench PC's licensed SDK/driver and matching provider."
