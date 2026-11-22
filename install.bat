@echo off
rem Budokai3-nx installer: double-click this file.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1" %*
echo.
pause
