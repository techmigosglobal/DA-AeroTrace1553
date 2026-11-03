param([string]$QtRoot='', [string]$CompilerRoot='C:\Qt\Tools\mingw1310_64',[ValidateRange(1,32)][int]$Jobs=1)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(!$QtRoot){
  $kit=Get-ChildItem 'C:\Qt' -Directory | Where-Object {$_.Name -match '^6\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'mingw_64\bin\qmake.exe'))} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
  if(!$kit){throw 'No Qt 6 MinGW x64 kit found under C:\Qt'}
  $QtRoot=Join-Path $kit.FullName 'mingw_64'
}
$build=Join-Path $projectRoot 'out\build\qwt-local'
$marker=Join-Path $build 'qt-root.txt'
$identity=[IO.Path]::GetFullPath($QtRoot)
if((Test-Path $marker) -and ((Get-Content $marker -Raw).Trim() -ne $identity)){
  $resolvedBuild=[IO.Path]::GetFullPath($build)
  $allowedBuild=[IO.Path]::GetFullPath((Join-Path $projectRoot 'out/build/qwt-local'))
  if($resolvedBuild -ne $allowedBuild){throw 'Refusing to remove an unexpected Qwt build directory'}
  Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}
$source=Join-Path $projectRoot 'third_party\qwt\src\src.pro'
if(!(Test-Path $source)){throw "Qwt source missing: $source"}
New-Item -ItemType Directory -Force (Join-Path $build 'src') | Out-Null
$env:PATH="$CompilerRoot\bin;$QtRoot\bin;$env:PATH"
Push-Location (Join-Path $build 'src')
try {
  & "$QtRoot\bin\qmake.exe" $source 'CONFIG+=release'
  if($LASTEXITCODE -ne 0){throw 'Qwt qmake failed'}
  & "$CompilerRoot\bin\mingw32-make.exe" -f Makefile.Release "-j$Jobs"
  if($LASTEXITCODE -ne 0){throw 'Qwt build failed'}
} finally {Pop-Location}
$identity | Set-Content -LiteralPath $marker -Encoding ASCII
Write-Output "Qwt: $build\lib\qwt.dll"
