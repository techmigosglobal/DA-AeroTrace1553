param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot/prepare-assistant.ps1"
$pins=Get-Content -LiteralPath (Join-Path $projectRoot 'tools/document_worker/assistant-assets.lock.json') -Raw | ConvertFrom-Json
$cache=Join-Path $projectRoot 'out/assistant-assets'
$runtime=Join-Path $cache 'runtime'
$model=Join-Path $cache $pins.model.file
$destination=Join-Path $PackageRoot 'assistant-runtime'
New-Item -ItemType Directory -Force (Join-Path $destination 'runtime') | Out-Null
foreach($entry in $pins.files.PSObject.Properties) {
  $source=Join-Path $cache $entry.Name
  if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $entry.Value){throw "Runtime member failed its hash check: $source"}
  Copy-Item -LiteralPath $source -Destination (Join-Path $destination $entry.Name) -Force
}
Copy-Item -LiteralPath $model -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'tools/document_worker/assistant-assets.lock.json') -Destination (Join-Path $destination 'manifest.json') -Force
Copy-Item -LiteralPath (Join-Path $cache 'Qwen-LICENSE.txt'),(Join-Path $cache 'llama-LICENSE.txt'),(Join-Path $runtime 'LICENSE-LLVM-OpenMP') -Destination (Join-Path $PackageRoot 'licenses') -Force
# App-local Microsoft CRT for the MSVC CPU runtime. The document packager has
# already verified the signatures and recorded their redistribution provenance.
foreach($name in 'msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll') {
  Copy-Item -LiteralPath (Join-Path $PackageRoot "document-runtime/$name") -Destination (Join-Path $destination 'runtime') -Force
}
$notice=@"
Offline ICD Assistant: Qwen3-1.7B Q4_K_M, Apache-2.0.
Maintainer quantization: https://huggingface.co/$($pins.model.repository)/tree/$($pins.model.revision)
Runtime: llama.cpp v0.9.0 resolves through nightly-tag.txt to $($pins.runtime.resolved_build), commit $($pins.runtime.revision), MIT.
Runtime source: https://github.com/ggml-org/llama.cpp/tree/$($pins.runtime.revision)
LLVM OpenMP remains subject to the bundled LICENSE-LLVM-OpenMP.
The full artifact identities, member hashes and resource policy are in assistant-runtime/manifest.json.
Generated explanations may be incomplete. Source citations and validated native mappings remain authoritative.
"@
$notice | Set-Content -LiteralPath (Join-Path $PackageRoot 'licenses/Offline-Assistant-NOTICE.txt') -Encoding UTF8
Write-Output ('Offline assistant staged bytes: '+(Get-ChildItem -LiteralPath $destination -Recurse -File | Measure-Object Length -Sum).Sum)
