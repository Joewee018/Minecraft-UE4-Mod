@echo off
rem Starts the Minecraft 1.20.1 dev client (superflat world) and opens the Unreal game window.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Play.ps1" -WorldType superflat -Restart
