param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$VcRedistDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$cache=Join-Path $projectRoot 'out/toolchains/downloads'
$wheelhouse=Join-Path $projectRoot 'out/document-wheelhouse-068'
$lock=Join-Path $projectRoot 'tools/document_worker/requirements-win-x64.lock.txt'
$pythonZip=Join-Path $cache 'python-3.14.7-embed-amd64.zip'
New-Item -ItemType Directory -Force $cache,$wheelhouse | Out-Null
if(!(Test-Path -LiteralPath $pythonZip)){
  Invoke-WebRequest -UseBasicParsing -Uri 'https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip' -OutFile $pythonZip
}
if((Get-FileHash -LiteralPath $pythonZip).Hash -ne 'd297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15'){
  throw 'Embedded Python SHA-256 mismatch. Remove the cached ZIP and retry.'
}
$runtime=Join-Path $PackageRoot 'document-runtime'
Expand-Archive -LiteralPath $pythonZip -DestinationPath $runtime
& py -3.14 -m pip install --disable-pip-version-check --no-index --find-links $wheelhouse --require-hashes --only-binary=:all: --no-compile --ignore-installed --no-deps -r $lock --target "$runtime/site-packages"
if($LASTEXITCODE -ne 0){
  & py -3.14 -m pip download --disable-pip-version-check --require-hashes --only-binary=:all: -r $lock -d $wheelhouse
  if($LASTEXITCODE -ne 0){throw 'Pinned Windows wheel download failed'}
  & py -3.14 -m pip install --disable-pip-version-check --no-index --find-links $wheelhouse --require-hashes --only-binary=:all: --no-compile --ignore-installed --no-deps -r $lock --target "$runtime/site-packages"
  if($LASTEXITCODE -ne 0){throw 'Pinned Windows wheel staging failed'}
}
& py -3.14 (Join-Path $PSScriptRoot 'compact-document-runtime.py') $runtime
if($LASTEXITCODE -ne 0){throw 'Compacting the document runtime failed'}
$workerSource=Join-Path $projectRoot 'tools/document_worker'
$workerDestination=Join-Path $PackageRoot 'document-worker'
New-Item -ItemType Directory -Force $workerDestination | Out-Null
foreach($name in 'worker.py','review_actions.py','structured_answers.py','local_generation.py','library_history.py','engineering_reference.py','retrieval_context.py'){
  Copy-Item -LiteralPath (Join-Path $workerSource $name) -Destination $workerDestination -Recurse
}
New-Item -ItemType Directory -Force "$workerDestination/models" | Out-Null
Copy-Item -LiteralPath "$workerSource/models/en_PP-OCRv4_rec_mobile.onnx" -Destination "$workerDestination/models"
Copy-Item -LiteralPath "$runtime/LICENSE.txt" -Destination "$PackageRoot/licenses/Python-LICENSE.txt" -Force
if(!$VcRedistDirectory){
  $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if(!(Test-Path -LiteralPath $vswhere)){throw 'Set -VcRedistDirectory to the x64 Microsoft C++ CRT redistributable folder'}
  $vsRoot=(& $vswhere -latest -products '*' -property installationPath).Trim()
  $version=Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory | Where-Object {$_.Name -match '^\d+\.'} | Sort-Object Name -Descending | Select-Object -First 1
  $VcRedistDirectory=Get-ChildItem -LiteralPath (Join-Path $version.FullName 'x64') -Directory | Where-Object {$_.Name -match '^Microsoft\.VC\d+\.CRT$'} | Select-Object -First 1 -ExpandProperty FullName
}
$redistManifest=@()
foreach($name in @('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll')){
  $source=Join-Path $VcRedistDirectory $name
  if(!(Test-Path -LiteralPath $source)){throw ('Missing OCR runtime dependency: '+$source)}
  $signature=Get-AuthenticodeSignature -LiteralPath $source
  if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft'){throw ('Unverified Microsoft CRT: '+$source)}
  Copy-Item -LiteralPath $source -Destination $runtime -Force
  $redistManifest+=@{file=$name;version=(Get-Item -LiteralPath $source).VersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;source=$source}
}
$redistManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$PackageRoot\licenses\Microsoft-CRT-provenance.json" -Encoding UTF8
'Microsoft C++ runtime redistributable files are included app-local for the PDF/OCR runtime. Source: installed Visual Studio Build Tools x64 CRT redistributable directory. These files remain subject to Microsoft Software License Terms. https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files' | Set-Content -LiteralPath "$PackageRoot\licenses\Microsoft-CRT-NOTICE.txt" -Encoding UTF8
$env:PYTHONNOUSERSITE='1'
$env:PYTHONDONTWRITEBYTECODE='1'
$health='{"op":"health"}' | & "$runtime\python.exe" "$PackageRoot\document-worker\worker.py"
if($LASTEXITCODE -ne 0){throw 'Packaged document runtime failed its health check'}
Write-Output $health
$testScript=Join-Path $workerSource 'tests.py'
& "$runtime/python.exe" -B -c 'import pathlib, runpy, sys, worker; assert pathlib.Path(worker.__file__).resolve().parent == pathlib.Path(sys.argv[2]).resolve(); script = sys.argv[1]; sys.argv = [script]; runpy.run_path(script, run_name=__name__)' $testScript $workerDestination
if($LASTEXITCODE -ne 0){throw 'Exact packaged runtime failed offline document tests'}
