@echo off
rem Installs Furoshiki's OCR helper beside this folder (see install.ps1). No administrator is needed.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
