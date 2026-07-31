@echo off
rem Double-click launcher for build.ps1 (no execution-policy friction).
rem   build.bat                -> interactive menu
rem   build.bat -Target All    -> build all four firmwares
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
pause
