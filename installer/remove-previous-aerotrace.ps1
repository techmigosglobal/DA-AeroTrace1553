[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$NewVersion,
  [Parameter(Mandatory=$true)][string]$InstallDirectory,
  [switch]$ShowProgress,
  [switch]$InspectOnly
)
$ErrorActionPreference='Stop'
$identity='Software\Microsoft\Windows\CurrentVersion\Uninstall\AeroTrace1553_Desktop_is1'

function Get-AeroTraceRegistration {
  foreach($view in [Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32){
    $machine=[Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine',$view)
    try {
      $key=$machine.OpenSubKey($identity)
      if($key){
        try {
          [pscustomobject]@{Version=$key.GetValue('DisplayVersion');Publisher=$key.GetValue('Publisher')
            Location=$key.GetValue('InstallLocation');Command=$key.GetValue('UninstallString')}
        } finally {$key.Dispose()}
      }
    } finally {$machine.Dispose()}
  }
}

function Assert-TrustedPath([string]$Path,[bool]$Directory) {
  $admins=@('S-1-5-18','S-1-5-32-544','S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464')
  $item=Get-Item -LiteralPath $Path -Force
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){
    throw 'The previous installation uses a redirected path. Remove it through Windows Installed apps.'
  }
  $acl=Get-Acl -LiteralPath $Path
  $owner=$acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
  if($owner -notin $admins){
    throw 'Windows does not report an administrative owner for the previous installation. Use Installed apps to remove it.'
  }
  $mask=[int][Security.AccessControl.FileSystemRights]::Write -bor
    [int][Security.AccessControl.FileSystemRights]::Delete -bor
    [int][Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
    [int][Security.AccessControl.FileSystemRights]::ChangePermissions -bor
    [int][Security.AccessControl.FileSystemRights]::TakeOwnership
  # An existing protected child under a volume root can coexist with rights
  # to create other root folders. The root must still prevent replacement.
  if($Directory -and $Path -eq [IO.Path]::GetPathRoot($Path)){
    $mask=[int][Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
      [int][Security.AccessControl.FileSystemRights]::ChangePermissions -bor
      [int][Security.AccessControl.FileSystemRights]::TakeOwnership
  }
  foreach($rule in $acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])){
    if($rule.AccessControlType -eq 'Allow' -and $rule.IdentityReference.Value -notin $admins -and
        !($rule.PropagationFlags -band [Security.AccessControl.PropagationFlags]::InheritOnly) -and
        ([int]$rule.FileSystemRights -band $mask)){
      throw 'The previous executable or directory is writable by another account. Use Windows Installed apps to remove it.'
    }
  }
}

function Get-PlainFiles([string]$Path) {
  $item=Get-Item -LiteralPath $Path -Force
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){
    throw 'A private application directory contains a redirected path. Move your data out and remove the old version manually.'
  }
  if(!$item.PSIsContainer){$item;return}
  foreach($child in Get-ChildItem -LiteralPath $Path -Force){
    if($child.PSIsContainer -or ($child.Attributes -band [IO.FileAttributes]::ReparsePoint)){
      Get-PlainFiles $child.FullName
    } else {$child}
  }
}

try {
  $entries=@(Get-AeroTraceRegistration)
  if($entries.Count -eq 0){throw 'The registered AeroTrace installation could not be identified.'}
  if($entries.Count -ne 1){throw 'Multiple AeroTrace installations are registered. Resolve them through Windows Installed apps.'}
  $old=$entries[0]
  if($old.Publisher -ne 'AeroTrace' -or $old.Version -notmatch '^\d+\.\d+\.\d+$' -or !$old.Location -or
      $old.Command -notmatch '^"([^"]+\\unins\d{3}\.exe)"$'){
    throw 'The registered AeroTrace uninstaller is missing or untrusted. Use Windows Installed apps.'
  }
  $uninstaller=[IO.Path]::GetFullPath($Matches[1])
  if([version]$old.Version -gt [version]$NewVersion){throw 'A newer AeroTrace version is installed. Install a matching or newer build.'}
  $oldDir=[IO.Path]::GetFullPath($old.Location).TrimEnd('\')
  $target=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
  if((Split-Path -LiteralPath $uninstaller) -ne $oldDir -or $target -ne $oldDir){
    throw 'The uninstaller and upgrade destination must match the registered AeroTrace directory.'
  }
  if(![IO.Directory]::Exists($oldDir) -or ![IO.File]::Exists($uninstaller)){
    throw 'The registered AeroTrace uninstaller is missing. Repair or remove the old version through Windows Installed apps.'
  }
  $oldApp=Join-Path $oldDir 'AeroTrace1553.exe'
  if(![IO.File]::Exists($oldApp) -or (Get-Item -LiteralPath $oldApp).VersionInfo.ProductName -ne 'AeroTrace 1553'){
    throw 'The registered location does not contain a recognized AeroTrace desktop executable.'
  }
  for($directory=[IO.DirectoryInfo]::new($oldDir);$null -ne $directory;$directory=$directory.Parent){
    Assert-TrustedPath $directory.FullName $true
  }
  Assert-TrustedPath $uninstaller $false
  Assert-TrustedPath ([IO.Path]::ChangeExtension($uninstaller,'dat')) $false
  Assert-TrustedPath $oldApp $false

  # Prior uninstallers recursively remove these two app-private trees.
  # The installed manifest identifies their payload; block unknown data.
  $known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $manifest=Join-Path $oldDir 'SHA256SUMS.txt'
  if(Test-Path -LiteralPath $manifest -PathType Leaf){
    Assert-TrustedPath $manifest $false
    foreach($line in Get-Content -LiteralPath $manifest){
      if($line -match '^[a-fA-F0-9]{64}  (.+)$'){[void]$known.Add($Matches[1].Replace('/','\'))}
    }
  }
  foreach($private in 'document-runtime','document-worker','assistant-runtime'){
    $tree=Join-Path $oldDir $private
    if(!(Test-Path -LiteralPath $tree)){continue}
    foreach($file in Get-PlainFiles $tree){
      $relative=$file.FullName.Substring($oldDir.Length+1)
      if(!$known.Contains($relative) -and !($file.Extension -eq '.pyc' -and $file.Directory.Name -eq '__pycache__')){
        throw "A private runtime folder contains an untracked file. Move user data out before uninstalling: $relative"
      }
    }
  }
  $running=Get-CimInstance Win32_Process -Filter "Name = 'AeroTrace1553.exe' OR Name = 'aerotrace_ddc_helper_x86.exe' OR Name = 'llama-server.exe'"
  foreach($process in $running){
    if(!$process.ExecutablePath -or $process.ExecutablePath.StartsWith($oldDir+'\',[StringComparison]::OrdinalIgnoreCase)){
      throw 'Stop and save any active capture, disconnect, and exit AeroTrace before installing.'
    }
  }
  if($InspectOnly){Write-Output 'Previous install validated. No application files were changed.';exit 0}

  Write-Output "Removing AeroTrace $($old.Version). Windows will retain the AeroTrace user-data folders."
  $switches=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/LOG')
  $style='Hidden'
  if($ShowProgress){$switches[0]='/SILENT';$style='Normal'}
  $process=Start-Process -FilePath $uninstaller -ArgumentList $switches -WorkingDirectory $oldDir -WindowStyle $style -Wait -PassThru
  if($process.ExitCode -ne 0){throw "Previous uninstall returned exit code $($process.ExitCode). Review the uninstall log before retrying."}
  $rename=Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue
  if($rename.PendingFileRenameOperations | Where-Object {$_ -and $_.IndexOf($oldDir+'\',[StringComparison]::OrdinalIgnoreCase) -ge 0}){
    Write-Output 'Windows queued old AeroTrace files for removal. Restart Windows before retrying setup.'
    exit 30
  }
  if(@(Get-AeroTraceRegistration).Count -or (Test-Path -LiteralPath $oldApp) -or (Test-Path -LiteralPath $uninstaller)){
    throw 'Previous AeroTrace removal is incomplete. Review the uninstall log before retrying.'
  }
  foreach($tree in 'providers','helpers','document-runtime','document-worker','assistant-runtime'){
    $remaining=Join-Path $oldDir $tree
    if((Test-Path -LiteralPath $remaining) -and (Get-ChildItem -LiteralPath $remaining -Force | Select-Object -First 1)){
      throw "Old application files remain in $tree. Preserve any custom packs or data and choose a clean installation directory."
    }
  }
  Write-Output 'Previous version removed.'
  exit 0
} catch {
  Write-Output $_.Exception.Message
  exit 20
}
