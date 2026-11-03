param([string]$BuildDirectory='out/build/backend-clang',[string]$LlvmRoot='out/toolchains/llvm-22.1.0',[ValidateRange(1,32)][int]$Jobs=1,[switch]$Test)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(![IO.Path]::IsPathRooted($LlvmRoot)){$LlvmRoot=Join-Path $project $LlvmRoot}
if(![IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $project $BuildDirectory}
$clang=Join-Path $LlvmRoot 'bin/clang-cl.exe'
if(!(Test-Path -LiteralPath $clang)){throw 'BLOCKED: LLVM 22.1.0 is missing; run setup-verification-tools.ps1'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'BLOCKED: Visual Studio x64 C++ Build Tools are required'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force $BuildDirectory | Out-Null
$environmentScript=Join-Path $BuildDirectory 'init-msvc.cmd'
[IO.File]::WriteAllLines($environmentScript,@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b 1','set'))
$lines=& $env:ComSpec /d /c call $environmentScript
if($LASTEXITCODE -ne 0){throw 'MSVC environment initialization failed'}
foreach($line in $lines){$at=$line.IndexOf('=');if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}}
$env:PATH=(Join-Path $LlvmRoot 'bin')+';'+(Join-Path $LlvmRoot 'lib/clang/22/lib/windows')+';C:\Qt\Tools\Ninja;'+$env:PATH
$cmake='C:\Qt\Tools\CMake_64\bin\cmake.exe'
& $cmake -S $project -B $BuildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_LINKER=$LlvmRoot/bin/lld-link.exe" '-DAEROTRACE_BUILD_GUI=OFF' '-DAEROTRACE_ENABLE_DDC=OFF' '-DAEROTRACE_SANITIZERS=ON' '-DAEROTRACE_BUILD_FUZZERS=ON' '-DAEROTRACE_RELEASE_IPO=OFF'
if($LASTEXITCODE -ne 0){throw 'Sanitizer configure failed'}
& $cmake --build $BuildDirectory --parallel $Jobs
if($LASTEXITCODE -ne 0){throw 'Sanitizer build failed'}
if($Test){& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir $BuildDirectory --output-on-failure;if($LASTEXITCODE -ne 0){throw 'Sanitizer tests failed'}}
