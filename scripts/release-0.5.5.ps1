$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$desktop=[Environment]::GetFolderPath('Desktop')
$setup=Join-Path $project 'dist\installers\AeroTrace1553-0.5.5-Setup-x64.exe'
$package=Join-Path $project 'dist\AeroTrace1553-0.5.5'
$final=Join-Path $env:ProgramFiles 'AeroTrace1553'
$test=Join-Path $env:ProgramFiles 'AeroTrace1553-UninstallCheck-055'
$receiptRoot=Join-Path $env:USERPROFILE 'Documents\AeroTrace-0.5.5-Release-Evidence'
New-Item -ItemType Directory -Force $receiptRoot | Out-Null
if(!(Test-Path -LiteralPath $setup)){throw 'Verified installer missing'}
function Wait-App([string]$File,[string[]]$Arguments){
 $process=Start-Process -FilePath $File -ArgumentList $Arguments -PassThru -Wait
 if($process.ExitCode -ne 0){throw "Process failed with $($process.ExitCode): $File"}
}
foreach($name in @('AeroTrace1553','aerotrace_desktop')){
 foreach($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)){
  if(!$process.CloseMainWindow() -or !$process.WaitForExit(30000)){throw 'Close the application before installing; an active capture may need saving'}
 }
}
# Remove registered old installations, never recordings/preferences stored outside the app.
$old=@(Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue | Where-Object {$_.DisplayName -like 'AeroTrace 1553*'})
$old | Select-Object DisplayName,DisplayVersion,InstallLocation,UninstallString | ConvertTo-Json | Set-Content (Join-Path $receiptRoot 'old-installations.json')
foreach($entry in $old){
 $uninstaller=$entry.UninstallString.Trim('"')
 if(!(Test-Path -LiteralPath $uninstaller)){throw "Registered uninstaller missing: $uninstaller"}
 Wait-App $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
 $location=$entry.InstallLocation.TrimEnd('\')
 if($location -and (Test-Path -LiteralPath $location)){
  $unexpected=@(Get-ChildItem -LiteralPath $location -Recurse -File -Force | Where-Object {$_.FullName -notmatch '\\__pycache__\\'})
  if($unexpected.Count){throw "Uninstaller left non-cache files; inspect: $($unexpected[0].FullName)"}
  Remove-Item -LiteralPath $location -Recurse -Force
 }
}
foreach($path in @((Join-Path $env:LOCALAPPDATA 'Programs\AeroTrace1553'),(Join-Path ${env:ProgramFiles(x86)} 'AeroTrace1553'),$final)){
 if(Test-Path -LiteralPath $path){throw "Unregistered application directory remains; inspect before overwriting: $path"}
}
if(Test-Path -LiteralPath $test){throw "Uninstall test directory already exists: $test"}
Wait-App $setup @('/VERYSILENT','/SP-','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=`"$test`"","/LOG=`"$receiptRoot\uninstall-test-setup.log`"")
# Generate Python bytecode to prove the uninstaller cleans generated caches too.
& "$test\document-runtime\python.exe" -c 'import collections, email, json, numpy'
if($LASTEXITCODE -ne 0){throw 'Uninstall test Python runtime failed'}
Wait-App (Join-Path $test 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
if(Test-Path -LiteralPath $test){throw "0.5.5 uninstaller left files: $test"}
if(Test-Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\AeroTrace1553.exe'){throw 'Uninstaller left App Paths registration'}
Wait-App $setup @('/VERYSILENT','/SP-','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$receiptRoot\final-setup.log`"")
$manifest=Get-Content (Join-Path $final 'deployment.json') -Raw | ConvertFrom-Json
if($manifest.version -ne '0.5.5'){throw 'Wrong deployed version'}
$version=(Get-Item (Join-Path $final 'AeroTrace1553.exe')).VersionInfo
if($version.FileVersion -ne '0.5.5' -or $version.ProductVersion -ne '0.5.5'){throw 'Wrong Windows executable version'}
foreach($name in @('AeroTrace1553.exe','emacepl.dll','qwt.dll','deployment.json')){
 if((Get-FileHash (Join-Path $final $name)).Hash -ne (Get-FileHash (Join-Path $package $name)).Hash){throw "Installed/package mismatch: $name"}
}
$appPath=(Get-Item 'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\AeroTrace1553.exe').GetValue('')
if($appPath -ne (Join-Path $final 'AeroTrace1553.exe')){throw 'Incorrect system App Paths registration'}
$registered=@(Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue | Where-Object {$_.DisplayName -like 'AeroTrace 1553*'})
if($registered.Count -ne 1 -or $registered[0].DisplayVersion -ne '0.5.5'){throw 'Unexpected remaining installer registrations'}
Copy-Item -LiteralPath $setup -Destination $desktop -Force
Copy-Item -LiteralPath ($setup+'.sha256') -Destination $desktop -Force
@{version='0.5.5';installed=$final;oldInstallationsRemoved=$true;uninstallRoundTripPassed=$true;pythonCachesRemoved=$true;appPaths=$appPath;registeredInstallations=$registered.Count;preferencesAndRecordingsPreserved=$true;desktopInstaller=(Join-Path $desktop (Split-Path $setup -Leaf))} | ConvertTo-Json | Set-Content (Join-Path $receiptRoot 'installation-receipt.json')
Write-Output "PASS: 0.5.5 installed at $final; old installations removed; uninstall round-trip clean; installer copied to Desktop"
