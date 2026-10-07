# Starts the Minecraft 1.20.1 Fabric dev client (real client + integrated server) for this project only.
# Runs in Work\mc; creates a fresh disposable superflat world. Returns once the bridge endpoint exists.
param([int]$TimeoutSec=420,[ValidateSet('superflat','debug')][string]$WorldType='superflat')
. "$PSScriptRoot\Common.ps1"
$Mc=Join-Path $Work 'mc'; New-Item -ItemType Directory -Force -Path $Mc | Out-Null
$Endpoint=Join-Path $Work 'bridge-endpoint.json'
# Loom passes JVM properties through its launch.cfg, so match the project path in the DLI config argument.
Set-Content -Path (Join-Path $Mc 'crb-world-type.txt') -Value $WorldType -Encoding ASCII
$running=@(Get-McClients)
if($running.Count -gt 0){ "Minecraft dev client already running (pid $($running[0].ProcessId))"; exit 0 }
# Options needed for an unfocused, host-driven client. Other settings are left as Minecraft writes them.
$opts=Join-Path $Mc 'options.txt'
$want=[ordered]@{ 'pauseOnLostFocus'='false'; 'onboardAccessibility'='false'; 'skipMultiplayerWarning'='true'; 'tutorialStep'='none';
  'joinedFirstServer'='true'; 'narrator'='0'; 'renderDistance'='6'; 'simulationDistance'='5'; 'maxFps'='60'; 'soundCategory_master'='0.3'; 'bobView'='true'; 'fullscreen'='false' }
$lines=@(); if(Test-Path $opts){ $lines=Get-Content $opts }
foreach($k in $want.Keys){ $lines=@($lines | ? { -not $_.StartsWith("${k}:") }); $lines+="${k}:$($want[$k])" }
Set-Content -Path $opts -Value $lines -Encoding ASCII
if(Test-Path $Endpoint){ Remove-Item $Endpoint -Force }
$log=Join-Path $Logs 'minecraft.log'; $err=Join-Path $Logs 'minecraft-err.log'
# Launched through ShellExecute (no -Redirect*): the client must not inherit the job runner's pipes,
# otherwise the job never completes while Minecraft runs. cmd does the log redirection itself.
$cmdLine="/c set ""JAVA_HOME=$env:JAVA_HOME"" && set ""GRADLE_USER_HOME=$env:GRADLE_USER_HOME"" && cd /d ""$Root\Bridge"" && gradlew.bat --no-daemon --console=plain runClient > ""$log"" 2> ""$err"""
$p=Start-Process -FilePath 'cmd.exe' -ArgumentList $cmdLine -WindowStyle Minimized -PassThru
"launcher pid $($p.Id)"
$deadline=(Get-Date).AddSeconds($TimeoutSec)
while((Get-Date) -lt $deadline -and -not (Test-Path $Endpoint)){ Start-Sleep 3 }
if(Test-Path $Endpoint){ "endpoint ready: $(Get-Content $Endpoint)" -replace '"token":"[0-9a-f]+"','"token":"<hidden>"' } else { "endpoint not ready after $TimeoutSec s"; Get-Content $log -Tail 40; exit 1 }
exit 0
