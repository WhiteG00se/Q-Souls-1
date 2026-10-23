@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Open-SaveFolder.ps1"
if errorlevel 1 (
    pause
    exit /b 1
)
exit /b 0
