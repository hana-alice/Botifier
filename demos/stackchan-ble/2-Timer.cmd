@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-Demo.ps1" -Demo Timer -Seconds 10
pause
