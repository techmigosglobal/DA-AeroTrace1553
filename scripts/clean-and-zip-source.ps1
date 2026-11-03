param([switch]$CleanProject)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$desktop=[Environment]::GetFolderPath('Desktop')
$evidence=Join-Path $env:USERPROFILE 'Documents\AeroTrace-0.5.5-Release-Evidence'
$archive=Join-Path $env:USERPROFILE 'Documents\AeroTrace-0.5.5-Build-Archive'
$staging=Join-Path $env:TEMP ('AeroTrace055-source-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $staging 'AeroTrace1553-0.5.5-Source'
New-Item -ItemType Directory -Force $source,$evidence,$archive | Out-Null
$folders=@('.codebase-memory','apps','modules','tools','scripts','installer','docs','examples','third_party','Concept_Boards','UI_Screens')
foreach($folder in $folders){
 $from=Join-Path $root $folder
 if(!(Test-Path -LiteralPath $from)){continue}
 & robocopy $from (Join-Path $source $folder) /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .git __pycache__ .moc .obj obj release debug build /XF *.obj *.o *.a *.lib *.dll *.exe *.pdb *.pch *.pyc *.ilk Makefile Makefile.Debug Makefile.Release .qmake.stash
 if($LASTEXITCODE -ge 8){throw "Source staging failed: $folder"}
}
Get-ChildItem -LiteralPath $root -File -Force | Where-Object {$_.Name -eq '.gitignore' -or $_.Name -like 'CMake*' -or $_.Extension -in @('.md','.cmd')} | Copy-Item -Destination $source
if(!(Test-Path "$source\third_party\qwt\src\src.pro")){throw 'Qwt source missing from staging'}
if((Get-Content "$source\CMakeLists.txt" -Raw) -notmatch 'VERSION 0.5.5'){throw 'Wrong staged source version'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$zip=Join-Path $desktop 'AeroTrace1553-0.5.5-Source.zip'
if(Test-Path -LiteralPath $zip){throw "Desktop ZIP already exists: $zip"}
$zipWriter=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
try {
 $prefix=$staging.TrimEnd('\')+'\'
 foreach($file in Get-ChildItem -LiteralPath $staging -Recurse -File -Force){
  $relative=$file.FullName.Substring($prefix.Length).Replace('\','/')
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zipWriter,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
 }
} finally {$zipWriter.Dispose()}
$reader=[IO.Compression.ZipFile]::OpenRead($zip)
try{
 $files=@($reader.Entries | Where-Object {$_.Name})
 if(!($files.FullName -contains 'AeroTrace1553-0.5.5-Source/CMakeLists.txt')){throw 'ZIP root project missing'}
 if($files.FullName -match '(?i)(^|/)(out|dist|build|\.git|__pycache__)/|\.(exe|dll|obj|pyc)$'){throw 'ZIP contains generated files'}
 $entry=$reader.GetEntry('AeroTrace1553-0.5.5-Source/CMakeLists.txt');$textReader=New-Object IO.StreamReader($entry.Open())
 try{if($textReader.ReadToEnd() -notmatch 'VERSION 0.5.5'){throw 'ZIP version mismatch'}}finally{$textReader.Dispose()}
 $count=$files.Count
}finally{$reader.Dispose()}
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | ForEach-Object {"$($_.Hash)  $([IO.Path]::GetFileName($_.Path))"} | Set-Content ($zip+'.sha256') -Encoding ASCII
# Preserve test captures and evidence before moving generated project directories.
$loopback=Join-Path $root 'out\investigation-20261102\loopback-csv'
if(Test-Path -LiteralPath $loopback){Copy-Item -LiteralPath $loopback -Destination (Join-Path $evidence 'Loopback-CSVs') -Recurse -Force}
if(Test-Path "$root\out\build\desktop-local\Testing\Temporary\LastTest.log"){Copy-Item "$root\out\build\desktop-local\Testing\Temporary\LastTest.log" (Join-Path $evidence 'CTest-0.5.5.log') -Force}
if($CleanProject){
 foreach($name in @('out','dist','build','.qtcreator','lib','RTPRO_4.1.1_28102026')){
  $path=Join-Path $root $name
  if(Test-Path -LiteralPath $path){
   $destination=Join-Path $archive $name
   try {
    if(Test-Path -LiteralPath $destination){throw 'Resume archive with file move'}
    Move-Item -LiteralPath $path -Destination $destination -ErrorAction Stop
   } catch {
    & robocopy $path $destination /E /MOVE /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if($LASTEXITCODE -ge 8){throw "Archive move failed: $path"}
   }
  }
 }
}
Remove-Item -LiteralPath $staging -Recurse -Force
@{sourceZip=$zip;files=$count;bytes=(Get-Item $zip).Length;sha256=(Get-FileHash $zip).Hash;project=$root;buildArchive=$archive;loopbackCsvs=(Join-Path $evidence 'Loopback-CSVs');projectCleaned=[bool]$CleanProject} | ConvertTo-Json | Set-Content (Join-Path $evidence 'source-zip-receipt.json')
Write-Output "PASS: $zip ($count files); generated outputs archived at $archive"
