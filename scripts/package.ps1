param([string]$QtRoot='',[string]$PackageName='AeroTrace1553',[string]$BuildDirectory='out\build\desktop-local')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(!$QtRoot){
  $kit=Get-ChildItem 'C:\Qt' -Directory | Where-Object {$_.Name -match '^6\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'mingw_64\bin\windeployqt.exe'))} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
  if(!$kit){throw 'No Qt 6 MinGW x64 kit found under C:\Qt'}
  $QtRoot=Join-Path $kit.FullName 'mingw_64'
}
$packageRoot=Join-Path $projectRoot ('dist\'+$PackageName)
if((Test-Path $packageRoot) -and @(Get-ChildItem -LiteralPath $packageRoot -Force).Count){throw "Choose a fresh PackageName; deployment folder is not empty: $packageRoot"}
New-Item -ItemType Directory -Force $packageRoot | Out-Null
$cache=Join-Path $projectRoot "$BuildDirectory\CMakeCache.txt"
if(!(Test-Path $cache)){throw "Build cache missing: $cache"}
$sdkLine=Get-Content $cache | Where-Object {$_ -match '^DDC_SDK_ROOT:PATH='} | Select-Object -First 1
$sdkRoot=if($sdkLine){$sdkLine.Substring('DDC_SDK_ROOT:PATH='.Length)}else{''}
$ddcCandidates=@("$sdkRoot\libraries\emacepl\bin\x64\Release\emacepl.dll","$sdkRoot\Lib\x64\Release\emacepl.dll")
$ddcRuntime=$ddcCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
if(!$ddcRuntime){throw "Matching x64 DDC runtime missing for configured SDK: $sdkRoot"}
@{schema='aerotrace.deployment.v1';version='0.5.5';architecture='x64';minimumWindows='Windows 10 1809';builtUtc=(Get-Date).ToUniversalTime().ToString('o');developmentFallbacks=$false;hardwareQualified=$false;qtVersion=(& "$QtRoot\bin\qmake.exe" -query QT_VERSION).Trim();ddcSdkRoot=$sdkRoot;ddcRuntimeSha256=(Get-FileHash -LiteralPath $ddcRuntime -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content -LiteralPath "$packageRoot\deployment.json" -Encoding UTF8
'[Paths]','Prefix=.' | Set-Content -LiteralPath "$packageRoot\qt.conf" -Encoding ASCII
Copy-Item -LiteralPath "$projectRoot\$BuildDirectory\apps\desktop\aerotrace_desktop.exe" -Destination "$packageRoot\AeroTrace1553.exe" -Force
$qwtRuntime=Join-Path $projectRoot 'out\build\qwt-local\lib\qwt.dll'
if(!(Test-Path -LiteralPath $qwtRuntime)){throw "Qt-matched Qwt runtime missing: $qwtRuntime"}
Copy-Item -LiteralPath $qwtRuntime -Destination $packageRoot
$env:PATH="C:\Qt\Tools\mingw1310_64\bin;$QtRoot\bin;"+$env:PATH
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$packageRoot\AeroTrace1553.exe"
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
Copy-Item -LiteralPath "$QtRoot\plugins\platforms\qoffscreen.dll" -Destination "$packageRoot\platforms" -Force
if(Test-Path -LiteralPath "$packageRoot\qwt.dll"){
  & "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$packageRoot\qwt.dll"
  if($LASTEXITCODE -ne 0){throw 'Qwt runtime deployment failed'}
}
Copy-Item -LiteralPath "$projectRoot\README.md" -Destination $packageRoot
Copy-Item -LiteralPath "$projectRoot\apps\desktop\icons\aerotrace.ico" -Destination "$packageRoot\aerotrace.ico" -Force
Copy-Item -LiteralPath "$projectRoot\AeroTrace_1553_PRD_v2.0_Vinay_Ravali.md" -Destination $packageRoot -Force
Copy-Item -LiteralPath "$projectRoot\docs" -Destination $packageRoot -Recurse -Force
Copy-Item -LiteralPath "$projectRoot\docs\Operator-Guide.html" -Destination "$packageRoot\START-HERE.html" -Force
# The full guide uses docs/assets; the entry page links to it without duplicating assets.
'<!doctype html><html lang="en"><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=docs/Operator-Guide.html"><title>AeroTrace guide</title><a href="docs/Operator-Guide.html">Open the AeroTrace operator guide</a></html>' | Set-Content -LiteralPath "$packageRoot\START-HERE.html" -Encoding UTF8
Copy-Item -LiteralPath "$projectRoot\examples" -Destination $packageRoot -Recurse -Force
New-Item -ItemType Directory -Force "$packageRoot\scripts" | Out-Null
Copy-Item -LiteralPath "$projectRoot\scripts\collect-diagnostics.ps1" -Destination "$packageRoot\scripts" -Force
'@echo off', 'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\collect-diagnostics.ps1"' | Set-Content -LiteralPath "$packageRoot\Collect Diagnostics.cmd" -Encoding ASCII
Copy-Item -LiteralPath "$projectRoot\scripts\check-ddc.ps1" -Destination "$packageRoot\scripts" -Force
New-Item -ItemType Directory -Force "$packageRoot\licenses" | Out-Null
$venvPython=Join-Path $projectRoot 'out\document-runtime\Scripts\python.exe'
$venvHealthy=$false
if(Test-Path $venvPython){try{& $venvPython -c 'import numpy, onnxruntime, pdfplumber' 2>$null;$venvHealthy=$LASTEXITCODE -eq 0}catch{$venvHealthy=$false}}
if($venvHealthy){& "$PSScriptRoot\package-documents.ps1" -PackageRoot $packageRoot}
else {
  $installed=Join-Path $env:ProgramFiles 'AeroTrace1553'
  if(!(Test-Path (Join-Path $installed 'document-runtime\python.exe'))){$installed=Join-Path $env:LOCALAPPDATA 'Programs\AeroTrace1553'}
  $installedPython=Join-Path $installed 'document-runtime\python.exe'
  if(!(Test-Path $installedPython)){throw 'No working document runtime. Run scripts/setup-documents.ps1 first.'}
  & $installedPython -c 'import numpy, onnxruntime, pdfplumber'
  if($LASTEXITCODE -ne 0){throw 'Installed document runtime is unhealthy'}
  Copy-Item -LiteralPath (Join-Path $installed 'document-runtime') -Destination $packageRoot -Recurse
  Copy-Item -LiteralPath (Join-Path $projectRoot 'tools\document_worker') -Destination (Join-Path $packageRoot 'document-worker') -Recurse
  Copy-Item -LiteralPath (Join-Path $installed 'licenses\Python-LICENSE.txt') -Destination "$packageRoot\licenses" -Force
  Copy-Item -LiteralPath (Join-Path $installed 'licenses\Microsoft-CRT-NOTICE.txt') -Destination "$packageRoot\licenses" -Force
  Copy-Item -LiteralPath (Join-Path $installed 'licenses\Microsoft-CRT-provenance.json') -Destination "$packageRoot\licenses" -Force
}
Copy-Item -LiteralPath $ddcRuntime -Destination $packageRoot -Force
$ddcLicense=@("$sdkRoot\docs\license.txt","$sdkRoot\Documentation\license.txt") | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if(!$ddcLicense){throw "DDC SDK license text missing: $sdkRoot"}
Copy-Item -LiteralPath $ddcLicense -Destination "$packageRoot\licenses\DDC-SDK-LICENSE.txt" -Force
Copy-Item -LiteralPath "$projectRoot\third_party\qwt\COPYING" -Destination "$packageRoot\licenses\Qwt-LICENSE.txt"
Get-ChildItem -LiteralPath "$QtRoot\..\..\Licenses" -File -ErrorAction SilentlyContinue | Copy-Item -Destination "$packageRoot\licenses"
if(Test-Path -LiteralPath "$QtRoot\..\..\Docs\Qt-6.12.0\qtdoc\lgpl.html"){
  Copy-Item -LiteralPath "$QtRoot\..\..\Docs\Qt-6.12.0\qtdoc\lgpl.html" -Destination "$packageRoot\licenses\Qt-LGPL.html"
}
$packagePrefix=[IO.Path]::GetFullPath($packageRoot).TrimEnd('\')+'\'
& "$packageRoot\document-runtime\python.exe" "$projectRoot\scripts\audit-pe-dependencies.py" $packageRoot (Join-Path $packageRoot 'dependency-audit.json')
if($LASTEXITCODE -ne 0){throw 'Package contains missing native imports; see dependency-audit.json'}
$hashLines=Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Where-Object {$_.Name -notin @('SHA256SUMS.txt','preferences.ini') -and $_.FullName -notmatch '\\(Sessions|Diagnostics|Logs|test-results|__pycache__)\\' -and $_.Extension -ne '.atd'} | Get-FileHash -Algorithm SHA256 | ForEach-Object {"$($_.Hash)  $($_.Path.Substring($packagePrefix.Length))"}
$hashLines | Set-Content "$packageRoot\SHA256SUMS.txt"
Write-Output "Launch: $packageRoot\AeroTrace1553.exe"
