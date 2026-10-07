# Shared paths for Crossover-Rebuilt jobs. Nothing here touches outputs\Crossover.
$Root=Split-Path $PSScriptRoot -Parent
$Logs=Join-Path $Root 'Logs'
$Work=Join-Path $Root 'Work'
$env:JAVA_HOME='C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot'
$env:GRADLE_USER_HOME=Join-Path $Work 'gh'
$env:Path="$env:JAVA_HOME\bin;$env:Path"
$UE='C:\Program Files\Epic Games\UE_4.27'
New-Item -ItemType Directory -Force -Path $Logs,$Work | Out-Null
function Assert-NotOld([string]$p){ $full=[IO.Path]::GetFullPath($p); $old=[IO.Path]::GetFullPath((Join-Path (Split-Path $Root -Parent) 'Crossover')); if($full.StartsWith($old+'\',[StringComparison]::OrdinalIgnoreCase) -or $full -ieq $old){ throw "Refusing to write into the reference project: $full" } }
function Get-McClients { @(Get-CimInstance Win32_Process -Filter "Name='java.exe'" | ? { $_.CommandLine -and $_.CommandLine.Contains('Crossover-Rebuilt\Bridge') -and $_.CommandLine -match 'devlaunchinjector|KnotClient' }) }
function Stop-McClients { foreach($p in Get-McClients){ "stopping Crossover-Rebuilt Minecraft client pid $($p.ProcessId)"; Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }; Start-Sleep 2; $e=Join-Path $Work 'bridge-endpoint.json'; if(Test-Path $e){ Remove-Item $e -Force } }
