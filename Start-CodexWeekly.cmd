@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Start-CodexWeekly.ps1" %*
set "weeklyExitCode=%errorlevel%"
if not "%weeklyExitCode%"=="0" pause
exit /b %weeklyExitCode%
