@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\collect-diagnostics.ps1" -ApplicationDirectory "%~dp0dist\AeroTrace1553"
