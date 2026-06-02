@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_release.ps1"
if errorlevel 1 (
  pause
  exit /b %errorlevel%
)
pause
