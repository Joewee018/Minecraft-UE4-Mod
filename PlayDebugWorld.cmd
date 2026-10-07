@echo off
rem Same as Play.cmd but in a vanilla Debug Mode world (every block state, spectator).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Play.ps1" -WorldType debug -Restart
