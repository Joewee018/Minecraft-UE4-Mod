@echo off
rem Runs the queued Crossover-Rebuilt job (Jobs\job.ps1). All output stays in this folder.
title Crossover-Rebuilt job
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\RunJob.ps1"
