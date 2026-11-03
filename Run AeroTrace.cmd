@echo off
if exist "%~dp0dist\AeroTrace1553\AeroTrace1553.exe" (
  start "AeroTrace 1553" "%~dp0dist\AeroTrace1553\AeroTrace1553.exe"
) else (
  echo Package not found. Run scripts\build.ps1 and scripts\package.ps1 first.
  pause
)
