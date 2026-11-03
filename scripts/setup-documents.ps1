param([string]$Python='python')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$environment=Join-Path $projectRoot 'out\document-runtime'
if(!(Test-Path -LiteralPath "$environment\Scripts\python.exe")) {
  & $Python -m venv $environment
  if($LASTEXITCODE -ne 0){throw 'Could not create isolated document runtime'}
}
$requirements=Join-Path $projectRoot 'tools\document_worker\requirements.lock.txt'
if(!(Test-Path -LiteralPath $requirements)){$requirements=Join-Path $projectRoot 'tools\document_worker\requirements.txt'}
& "$environment\Scripts\python.exe" -m pip install --disable-pip-version-check -r $requirements
if($LASTEXITCODE -ne 0){throw 'Document dependencies could not be installed'}
$models=Join-Path $projectRoot 'tools\document_worker\models'
New-Item -ItemType Directory -Force $models | Out-Null
$downloads=@(
  @{Name='en_PP-OCRv4_rec_mobile.onnx';Hash='e8770c967605983d1570cdf5352041dfb68fa0c21664f49f47b155abd3e0e318';Url='https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/rec/en_PP-OCRv4_rec_mobile.onnx'},
  @{Name='minilm-int8.onnx';Hash='b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21';Url='https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/321b1453a093b6ed637ddb765600dbdd288610c8/onnx/model_quint8_avx2.onnx'},
  @{Name='minilm-tokenizer.json';Hash='be50c3628f2bf5bb5e3a7f17b1f74611b2561a3a27eeab05e5aa30f411572037';Url='https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/321b1453a093b6ed637ddb765600dbdd288610c8/tokenizer.json'}
)
foreach($model in $downloads) {
  $destination=Join-Path $models $model.Name
  if(!(Test-Path -LiteralPath $destination)){Invoke-WebRequest -Uri $model.Url -OutFile $destination -UseBasicParsing}
  if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.Hash){throw "Model checksum mismatch: $($model.Name)"}
}
& "$environment\Scripts\python.exe" "$projectRoot\tools\document_worker\tests.py"
if($LASTEXITCODE -ne 0){throw 'Offline document tests failed'}
Write-Output "Document runtime ready: $environment"
