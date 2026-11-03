param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$VcRedistDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$environment=Join-Path $projectRoot 'out\document-runtime'
$python=Join-Path $environment 'Scripts\python.exe'
if(!(Test-Path -LiteralPath $python)){throw 'Run scripts/setup-documents.ps1 before packaging'}
$base=(& $python -c 'import sys; print(sys.base_prefix)').Trim()
$runtime=Join-Path $PackageRoot 'document-runtime'
New-Item -ItemType Directory -Force "$runtime\Lib" | Out-Null
Copy-Item -LiteralPath "$base\python.exe" -Destination $runtime -Force
Get-ChildItem -LiteralPath $base -Filter '*.dll' -File | Copy-Item -Destination $runtime -Force
& robocopy "$base\DLLs" "$runtime\DLLs" /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
if($LASTEXITCODE -ge 8){throw 'Python extension deployment failed'}
& robocopy "$base\Lib" "$runtime\Lib" /E /XD site-packages __pycache__ test tests idlelib tkinter turtledemo /NFL /NDL /NJH /NJS /NC /NS | Out-Null
if($LASTEXITCODE -ge 8){throw 'Python standard-library deployment failed'}
& robocopy "$environment\Lib\site-packages" "$runtime\Lib\site-packages" /E /XD __pycache__ /NFL /NDL /NJH /NJS /NC /NS | Out-Null
if($LASTEXITCODE -ge 8){throw 'Document dependency deployment failed'}
& robocopy "$projectRoot\tools\document_worker" "$PackageRoot\document-worker" /E /XD __pycache__ /NFL /NDL /NJH /NJS /NC /NS | Out-Null
if($LASTEXITCODE -ge 8){throw 'Document worker deployment failed'}
Copy-Item -LiteralPath "$base\LICENSE.txt" -Destination "$PackageRoot\licenses\Python-LICENSE.txt" -Force
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
