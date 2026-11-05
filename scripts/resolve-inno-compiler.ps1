function Resolve-InnoCompiler([string]$Requested='') {
  if($Requested){
    if(!(Test-Path -LiteralPath $Requested -PathType Leaf)){throw "Selected Inno Setup compiler does not exist: $Requested"}
    return (Resolve-Path -LiteralPath $Requested).Path
  }
  $candidates=@()
  if($env:AEROTRACE_ISCC){return Resolve-InnoCompiler $env:AEROTRACE_ISCC}
  $command=Get-Command ISCC.exe -CommandType Application -ErrorAction SilentlyContinue
  if($command){$candidates+=@($command | ForEach-Object {$_.Source})}
  $candidates+=Join-Path $env:LOCALAPPDATA 'AeroTraceBuildTools/InnoSetup/ISCC.exe'
  foreach($root in @($env:ProgramFiles,${env:ProgramFiles(x86)},(Join-Path $env:LOCALAPPDATA 'Programs'))){
    if($root){foreach($version in @(6,7)){$candidates+=Join-Path $root "Inno Setup $version/ISCC.exe"}}
  }
  foreach($candidate in $candidates){if(Test-Path -LiteralPath $candidate -PathType Leaf){return (Resolve-Path -LiteralPath $candidate).Path}}
  throw 'Inno Setup compiler not found. Install Inno Setup 6: winget install --id JRSoftware.InnoSetup -e -s winget -i ; or pass -Compiler <absolute-path-to-ISCC.exe>.'
}
