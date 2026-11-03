param(
  [string]$QtRoot = '',
  [string]$CompilerRoot = 'C:\Qt\Tools\mingw1310_64',
  [string]$BuildDirectory = 'out\build\desktop-local',
  [switch]$CoreOnly,
  [ValidateRange(1,32)][int]$Jobs=1,
  [switch]$Test
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if(!$CoreOnly -and !$QtRoot){
  $kit=Get-ChildItem 'C:\Qt' -Directory | Where-Object {$_.Name -match '^6\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'mingw_64\lib\cmake\Qt6\Qt6Config.cmake'))} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
  if(!$kit){throw 'No Qt 6 MinGW x64 kit found under C:\Qt'}
  $QtRoot=Join-Path $kit.FullName 'mingw_64'
}
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ninja = 'C:\Qt\Tools\Ninja\ninja.exe'
foreach ($required in @($cmake, $ninja, "$CompilerRoot\bin\g++.exe")) {
  if (!(Test-Path -LiteralPath $required)) { throw "Required tool not found: $required" }
}
if (!$CoreOnly -and !(Test-Path -LiteralPath "$QtRoot\lib\cmake\Qt6\Qt6Config.cmake")) {
  throw "Qt Widgets kit not found: $QtRoot"
}
$env:PATH = "$CompilerRoot\bin;$QtRoot\bin;" + $env:PATH
$relativeBuild = if($CoreOnly){'out\build\core-local'}else{$BuildDirectory}
$buildDir = if([IO.Path]::IsPathRooted($relativeBuild)){$relativeBuild}else{Join-Path $projectRoot $relativeBuild}
$gui = if ($CoreOnly) { 'OFF' } else { 'ON' }
$compilerCmake=$CompilerRoot.Replace('\','/')
if(!$CoreOnly){
  & "$PSScriptRoot\build-qwt.ps1" -QtRoot $QtRoot -CompilerRoot $CompilerRoot -Jobs $Jobs
  $qwtLibrary=(Join-Path $projectRoot 'out\build\qwt-local\lib\libqwt.a').Replace('\','/')
  if(!(Test-Path $qwtLibrary)){throw "Qt-matched Qwt library missing: $qwtLibrary"}
}
$cmakeArgs=@('-S',$projectRoot,'-B',$buildDir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_CXX_COMPILER=$compilerCmake/bin/g++.exe","-DCMAKE_RC_COMPILER=$compilerCmake/bin/windres.exe","-DAEROTRACE_BUILD_GUI=$gui")
if(!$CoreOnly){$cmakeArgs+=@("-DCMAKE_PREFIX_PATH=$QtRoot","-DQWT_LIBRARY=$qwtLibrary",'-DQWT_DEBUG_LIBRARY=QWT_DEBUG_LIBRARY-NOTFOUND')}
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
if (!$CoreOnly) {
  & $cmake --build $buildDir --target aerotrace_desktop_autogen
  if ($LASTEXITCODE -ne 0) { throw 'Designer form generation failed' }
}
& $cmake --build $buildDir --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Test) {
  & 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir $buildDir --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
Write-Output "Build: $buildDir"
