@echo off
rem Double-click launcher for setup.ps1 (no execution-policy friction).
rem One-time build environment setup: arduino-cli + esp32 core + libraries.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
pause
