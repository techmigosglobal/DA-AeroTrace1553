param([string]$OutputRoot='out/toolchains')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(![IO.Path]::IsPathRooted($OutputRoot)){$OutputRoot=Join-Path $project $OutputRoot}
$download=Join-Path $OutputRoot 'downloads';New-Item -ItemType Directory -Force $download | Out-Null
$name='LLVM-22.1.0-win64.exe';$file=Join-Path $download $name
& gh release download llvmorg-22.1.0 --repo llvm/llvm-project --pattern $name --pattern "$name.jsonl" --dir $download --skip-existing
if($LASTEXITCODE -ne 0){throw 'LLVM download failed'}
$expected='b31d5f54942e017cb878e594529723dd629cc7b54c9bf7a331e2dc01e8ea5e75'
if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw 'LLVM checksum mismatch'}
& gh attestation verify $file --repo llvm/llvm-project --bundle "$file.jsonl"
if($LASTEXITCODE -ne 0){throw 'LLVM provenance verification failed'}
$sevenZip=Join-Path $env:ProgramFiles '7-Zip/7z.exe'
if(!(Test-Path -LiteralPath $sevenZip)){throw 'BLOCKED: 7-Zip is required for local extraction'}
$destination=Join-Path $OutputRoot 'llvm-22.1.0'
& $sevenZip x $file "-o$destination" -y -bso0 -bsp0
if($LASTEXITCODE -ne 0){throw 'LLVM extraction failed'}
@{schema='aerotrace.toolchain.v1';status='PASS';version='22.1.0';sha256=$expected;source='llvm/llvm-project llvmorg-22.1.0';attestationVerified=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'llvm-receipt.json') -Encoding UTF8
