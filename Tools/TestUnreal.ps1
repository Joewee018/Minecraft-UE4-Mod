# Runs the packaged game's end-to-end suite against this project's live Minecraft dev client and stores a report
# tied to the exact game executable, pak and bridge jar hashes.
param([int]$TimeoutMin=25,[string]$Suite='all')
. "$PSScriptRoot\Common.ps1"
& "$PSScriptRoot\StartMinecraft.ps1"
if($LASTEXITCODE -ne 0){ "minecraft not available"; exit 1 }
$stamp=(Get-Date).ToString('yyyyMMdd-HHmmss')
$reports=Join-Path $Root 'Reports'; New-Item -ItemType Directory -Force -Path $reports | Out-Null
$runDir=Join-Path $reports "UnrealTest-$Suite-$stamp"; New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$mcShots=Join-Path $Work 'mc\screenshots'; $dbg=Join-Path $Work 'debug'
Get-ChildItem $mcShots -Filter 'mc_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $dbg -Filter '*.json' -ErrorAction SilentlyContinue | Remove-Item -Force
$exe=Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt.exe'
$bin=Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt\Binaries\Win64\CrossoverRebuilt.exe'
if(!(Test-Path $exe)){ "package missing: $exe"; exit 1 }
$endpoint=Join-Path $Work 'bridge-endpoint.json'
$report=Join-Path $runDir 'raw-report.json'
Get-Process CrossoverRebuilt -ErrorAction SilentlyContinue | ? { $_.Path -and $_.Path.StartsWith($Root) } | Stop-Process -Force
$crbAvatar=''
$skelJson=Join-Path $Work 'avatar\build\skeleton.json'
if(Test-Path $skelJson){ try { $crbAvatar=' -CrbAvatarCrc=' + ('{0:X8}' -f [uint32](Get-Content $skelJson -Raw | ConvertFrom-Json).skeleton_crc32) } catch {} }
$steveJson=Join-Path $Work 'steve\build\skeleton.json'
if(Test-Path $steveJson){ try { $crbAvatar+=' -CrbSteveCrc=' + (Get-Content $steveJson -Raw | ConvertFrom-Json).crc } catch {} }
$ppJson=Join-Path $Work 'pp\build\skeleton.json'
if(Test-Path $ppJson){ try { $crbAvatar+=' -CrbPPCrc=' + (Get-Content $ppJson -Raw | ConvertFrom-Json).crc } catch {} }
$argList="-windowed -ResX=1280 -ResY=720 -CrbEndpoint=`"$endpoint`" -CrbTest=$Suite -CrbReport=`"$report`" -CrbShots=`"$runDir`" -CrbQuit -log=CrbTest-$stamp.log$crbAvatar"
$testStart=Get-Date
$p=Start-Process -FilePath $exe -ArgumentList $argList -PassThru
"game pid $($p.Id)"
$deadline=(Get-Date).AddMinutes($TimeoutMin)
while(-not $p.HasExited -and (Get-Date) -lt $deadline){ Start-Sleep 5 }
if(-not $p.HasExited){ "test timed out; stopping game"; Stop-Process -Id $p.Id -Force }
Start-Sleep 2
Get-ChildItem $mcShots -Filter 'mc_*.png' -ErrorAction SilentlyContinue | Copy-Item -Destination $runDir
Get-ChildItem $dbg -Filter '*.json' -ErrorAction SilentlyContinue | % { Copy-Item $_.FullName (Join-Path $runDir ('java_' + $_.Name)) }
$log=Get-ChildItem (Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt\Saved\Logs') -Filter "CrbTest-$stamp*.log" -ErrorAction SilentlyContinue | Select -First 1
if($log){ Copy-Item $log.FullName (Join-Path $runDir 'game.log') }
$jar=Get-ChildItem (Join-Path $Root 'Bridge\build\libs') -Filter 'crossover-rebuilt-bridge-*.jar' | ? { $_.Name -notmatch 'sources' } | Select -First 1
$pak=Get-ChildItem (Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt\Content\Paks') -Filter *.pak | Select -First 1
$raw=$null; if(Test-Path $report){ $raw=Get-Content $report -Raw | ConvertFrom-Json }
# The full suite ends with Esc -> Game Menu -> Quit Game: Minecraft must save the world and exit, then Unreal exits.
$quit=$null
if($Suite -eq 'all'){
  $mcDeadline=(Get-Date).AddSeconds(45)
  while((Get-Date) -lt $mcDeadline -and @(Get-McClients).Count -gt 0){ Start-Sleep 2 }
  $mcLeft=@(Get-McClients).Count
  $lvl=Get-ChildItem (Join-Path $Work 'mc\saves') -Directory -ErrorAction SilentlyContinue | ? { $_.Name -like 'crb-*' } | % { Get-Item (Join-Path $_.FullName 'level.dat') -ErrorAction SilentlyContinue } | Sort-Object LastWriteTime -Descending | Select -First 1
  $saved=[bool]($lvl -and $lvl.LastWriteTime -gt $testStart)
  $quit=[ordered]@{ unrealExited=$p.HasExited; minecraftExited=($mcLeft -eq 0); worldSaved=$saved; levelDat=$(if($lvl){$lvl.FullName}else{''}); levelDatWritten=$(if($lvl){$lvl.LastWriteTime.ToString('o')}else{''}) }
  if($mcLeft -gt 0){ "Minecraft still running after Quit Game; stopping it"; Stop-McClients }
}
$out=[ordered]@{
  testedUtc=(Get-Date).ToUniversalTime().ToString('o'); suite=$Suite
  passed=[bool]($raw -and $raw.passed -and ($Suite -ne 'all' -or ($quit.minecraftExited -and $quit.worldSaved -and $quit.unrealExited)))
  quitViaPauseMenu=$quit
  gameExeSha256=(Get-FileHash $bin -Algorithm SHA256).Hash; pakSha256=(Get-FileHash $pak.FullName -Algorithm SHA256).Hash
  bridgeJar=$jar.Name; bridgeJarSha256=(Get-FileHash $jar.FullName -Algorithm SHA256).Hash
  exitCode=$p.ExitCode; report=$raw
}
$out | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $runDir 'UnrealTestReport.json') -Encoding UTF8
Set-Content (Join-Path $Logs 'last-unreal-test.txt') $runDir
"run dir: $runDir"
if($raw){ "passed=$($raw.passed) checks=$($raw.checks.Count) failures=$($raw.failures.Count)"; $raw.failures | % { "FAIL: $_" } } else { "no report written" }
if($quit){ "quit via pause menu: unrealExited=$($quit.unrealExited) minecraftExited=$($quit.minecraftExited) worldSaved=$($quit.worldSaved)" }
if($log){ Select-String -Path $log.FullName -Pattern 'Error|Fatal|Assertion|TEST FAIL' | Select -First 40 | % { $_.Line } }
exit 0
