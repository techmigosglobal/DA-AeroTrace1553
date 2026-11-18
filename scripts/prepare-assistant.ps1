param()
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$pins=Get-Content -LiteralPath (Join-Path $projectRoot 'tools/document_worker/assistant-assets.lock.json') -Raw | ConvertFrom-Json
$cache=Join-Path $projectRoot 'out/assistant-assets'
New-Item -ItemType Directory -Force $cache | Out-Null
function Get-PinnedFile([string]$Path,[string]$Url,[string]$Hash) {
  if(!(Test-Path -LiteralPath $Path)) {
    $partial=$Path+'.part'
    & curl.exe -L --fail --retry 3 --output $partial $Url
    if($LASTEXITCODE -ne 0){throw "Download failed: $Url"}
    if((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Hash){throw "Downloaded artifact failed its hash check: $Url"}
    Move-Item -LiteralPath $partial -Destination $Path -Force
  }
  if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Hash){throw "Assistant artifact SHA-256 mismatch: $Path"}
}
$model=Join-Path $cache $pins.model.file
Get-PinnedFile $model "https://huggingface.co/$($pins.model.repository)/resolve/$($pins.model.revision)/$($pins.model.file)" $pins.model.sha256
$archive=Join-Path $cache $pins.runtime.archive
Get-PinnedFile $archive "https://github.com/ggml-org/llama.cpp/releases/download/$($pins.runtime.resolved_build)/$($pins.runtime.archive)" $pins.runtime.sha256
$runtime=Join-Path $cache 'runtime'
if(!(Test-Path -LiteralPath (Join-Path $runtime 'llama-server.exe'))){Expand-Archive -LiteralPath $archive -DestinationPath $runtime -Force}
Get-PinnedFile (Join-Path $cache 'Qwen-LICENSE.txt') "https://huggingface.co/Qwen/Qwen3-1.7B/resolve/$($pins.model_license_revision)/LICENSE" $pins.model_license_sha256
Get-PinnedFile (Join-Path $cache 'llama-LICENSE.txt') "https://raw.githubusercontent.com/ggml-org/llama.cpp/$($pins.runtime.revision)/LICENSE" '94f29bbed6a22c35b992c5c6ebf0e7c92f13b836b90f36f461c9cf2f0f1d010d'
foreach($entry in $pins.files.PSObject.Properties) {
  $member=Join-Path $cache $entry.Name
  if((Get-FileHash -LiteralPath $member -Algorithm SHA256).Hash -ne $entry.Value){throw "Runtime member SHA-256 mismatch: $member"}
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'tools/document_worker/assistant-assets.lock.json') -Destination (Join-Path $cache 'manifest.json') -Force
Write-Output "Verified offline assistant cache: $cache"
