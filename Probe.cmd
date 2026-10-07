@echo off
rem Records toolchain versions into Logs\probe.txt. Writes only inside this folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Probe.ps1"
