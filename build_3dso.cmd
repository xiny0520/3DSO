@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\build.ps1" -BuildDir "%ROOT%\build_release_vs" -Config Release %*
exit /b %ERRORLEVEL%
