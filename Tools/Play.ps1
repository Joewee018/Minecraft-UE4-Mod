# Opens BOTH: the packaged Unreal game window and this project's Minecraft 1.20.1 dev client (Java side).
# The game window opens first and shows "Waiting for Minecraft" until the Java bridge is up (about 1 minute),
# then connects by itself. Everything printed here is also saved to Logs\play.log.
param([int]$ResX=1600,[int]$ResY=900,[ValidateSet('superflat','debug')][string]$WorldType='superflat',[switch]$Restart)
. "$PSScriptRoot\Common.ps1"
Start-Transcript -Path (Join-Path $Logs 'play.log') -Force | Out-Null
try {
  $exe=Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt.exe'
  if(!(Test-Path $exe)){ throw "Game not built yet: $exe" }
  Get-Process CrossoverRebuilt -ErrorAction SilentlyContinue | ? { $_.Path -and $_.Path.StartsWith($Root) } | Stop-Process -Force
  if($Restart){ Stop-McClients; Start-Sleep 2 }
  $endpoint=Join-Path $Work 'bridge-endpoint.json'
  if(@(Get-McClients).Count -eq 0 -and (Test-Path $endpoint)){ Remove-Item $endpoint -Force } # stale file from a closed client
  Write-Host "Opening the Crossover-Rebuilt game window..." -ForegroundColor Cyan
  $p=Start-Process -FilePath $exe -ArgumentList "-windowed -ResX=$ResX -ResY=$ResY -CrbEndpoint=`"$endpoint`"" -PassThru
  "game pid $($p.Id)"
  Write-Host "Starting Minecraft 1.20.1 ($WorldType world). The game connects automatically when it is ready (about 1 minute)..." -ForegroundColor Cyan
  & "$PSScriptRoot\StartMinecraft.ps1" -WorldType $WorldType
  if(@(Get-McClients).Count -eq 0){ throw "Minecraft did not start - see Logs\minecraft.log" }
  if($p.HasExited){ throw "The game window closed (exit code $($p.ExitCode)) - see Package\WindowsNoEditor\CrossoverRebuilt\Saved\Logs" }
  Write-Host "Both are running. This window closes in 5 seconds." -ForegroundColor Green
  Start-Sleep 5
} catch {
  Write-Host "PROBLEM: $_" -ForegroundColor Red
  Write-Host "Press Enter to close."; [void][Console]::ReadLine()
} finally { Stop-Transcript | Out-Null }
