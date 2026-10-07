# Builds Crossover-Rebuilt: editor target (for the asset/cook steps), creates original materials + map with the
# UE4 Python commandlet, then BuildCookRun packages a Win64 Development standalone game into Package\.
param([switch]$SkipAssets,[switch]$SkipPackage,[int]$MaxParallel=3)
. "$PSScriptRoot\Common.ps1"
$Proj=Join-Path $Root 'Unreal\CrossoverRebuilt.uproject'
$Build=Join-Path $UE 'Engine\Build\BatchFiles\Build.bat'
$UAT=Join-Path $UE 'Engine\Build\BatchFiles\RunUAT.bat'
$EditorCmd=Join-Path $UE 'Engine\Binaries\Win64\UE4Editor-Cmd.exe'
$t0=Get-Date
"== editor target"
& $Build CrossoverRebuiltEditor Win64 Development "-Project=$Proj" -WaitMutex -FromMsBuild "-MaxParallelActions=$MaxParallel"
if($LASTEXITCODE -ne 0){ "EDITOR BUILD FAILED $LASTEXITCODE"; exit 2 }
if(-not $SkipAssets){
  "== assets (python commandlet)"
  $script=Join-Path $Root 'Unreal\Scripts\CreateAssets.py'
  & $EditorCmd $Proj -run=pythonscript "-script=$script" -unattended -nopause -nosplash -NoSound -stdout -FullStdOutLogOutput 2>&1 | Select-String -Pattern 'CRB ASSET|CRB ASSETS|Error|error:|Traceback|Exception' | % { $_.Line }
  $mats=@(Get-ChildItem (Join-Path $Root 'Unreal\Content\Crb') -Filter *.uasset -ErrorAction SilentlyContinue)
  "materials on disk: $($mats.Count)"
  if($mats.Count -lt 8){ "ASSET STEP FAILED"; exit 3 }
  # Custom Avatar add-on: import the rigged mesh + clips when the Blender build produced them.
  $avBuild=Join-Path $Work 'avatar\build'
  if(Test-Path (Join-Path $avBuild 'fbx\Avatar.fbx')){
    "== avatar assets (python commandlet)"
    $env:CRB_AVATAR_BUILD=$avBuild
    & $EditorCmd $Proj -run=pythonscript "-script=$(Join-Path $Root 'Unreal\Scripts\CreateAvatar.py')" -unattended -nopause -nosplash -NoSound -stdout -FullStdOutLogOutput 2>&1 | Select-String -Pattern 'CRB AVATAR|Error|error:|Traceback|Exception' | % { $_.Line }
  } else { "avatar assets: no Avatar.fbx yet (mod row will report it)" }
  # SM64 Steve Movement add-on: the Steve rig + clips.
  $stBuild=Join-Path $Work 'steve\build'
  if(Test-Path (Join-Path $stBuild 'fbx\Steve.fbx')){
    "== steve assets (python commandlet)"
    $env:CRB_STEVE_BUILD=$stBuild
    & $EditorCmd $Proj -run=pythonscript "-script=$(Join-Path $Root 'Unreal\Scripts\CreateSteve.py')" -unattended -nopause -nosplash -NoSound -stdout -FullStdOutLogOutput 2>&1 | Select-String -Pattern 'CRB STEVE|Error|error:|Traceback|Exception' | % { $_.Line }
  } else { "steve assets: no Steve.fbx yet (run Tools\SteveBuild.ps1)" }
  # Physics & Portal mod: the user's sculpted 3D Steve (ragdoll physics asset) + the portal material.
  $ppBuild=Join-Path $Work 'pp\build'
  if(Test-Path (Join-Path $ppBuild 'fbx\Steve3D.fbx')){
    "== physics & portal assets (python commandlet)"
    $env:CRB_PP_BUILD=$ppBuild
    & $EditorCmd $Proj -run=pythonscript "-script=$(Join-Path $Root 'Unreal\Scripts\CreatePP.py')" -unattended -nopause -nosplash -NoSound -stdout -FullStdOutLogOutput 2>&1 | Select-String -Pattern 'CRB PP|Error|error:|Traceback|Exception' | % { $_.Line }
  } else { "pp assets: no Steve3D.fbx yet (run Tools\PPBuild.ps1)" }
  # Minecraft x Elden Combat: the same custom 3D Steve rig with the E_* combat clips, in its own folder (/Game/Crb/EC).
  if(Test-Path (Join-Path $ppBuild 'fbx\E_Idle.fbx')){
    "== elden combat assets (python commandlet)"
    $env:CRB_PP_BUILD=$ppBuild; $env:CRB_SCRIPT_DIR=Join-Path $Root 'Unreal\Scripts'
    & $EditorCmd $Proj -run=pythonscript "-script=$(Join-Path $Root 'Unreal\Scripts\CreateEC.py')" -unattended -nopause -nosplash -NoSound -stdout -FullStdOutLogOutput 2>&1 | Select-String -Pattern 'CRB EC|Error|error:|Traceback|Exception' | % { $_.Line }
  } else { "ec assets: no E_ clips yet (run Tools\PPBuild.ps1)" }
}
if(-not $SkipPackage){
  "== package"
  $out=Join-Path $Root 'Package'
  & $UAT BuildCookRun "-project=$Proj" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$out" -utf8output "-ubtargs=-MaxParallelActions=$MaxParallel" 2>&1 | Select-String -Pattern 'error|Error|BUILD SUCCESSFUL|AutomationTool exiting|Cook|Warning: ' | Select -Last 120 | % { $_.Line }
  $exe=Join-Path $out 'WindowsNoEditor\CrossoverRebuilt.exe'
  if(-not (Test-Path $exe)){ "PACKAGE FAILED"; exit 4 }
  $bin=Join-Path $out 'WindowsNoEditor\CrossoverRebuilt\Binaries\Win64\CrossoverRebuilt.exe'
  $pak=Get-ChildItem (Join-Path $out 'WindowsNoEditor\CrossoverRebuilt\Content\Paks') -Filter *.pak | Select -First 1
  $info=[ordered]@{ builtUtc=(Get-Date).ToUniversalTime().ToString('o'); exe=$exe; gameSha256=(Get-FileHash $bin -Algorithm SHA256).Hash; pakSha256=(Get-FileHash $pak.FullName -Algorithm SHA256).Hash; minutes=[math]::Round(((Get-Date)-$t0).TotalMinutes,1) }
  $info | ConvertTo-Json | Set-Content (Join-Path $Logs 'unreal-build.json')
  $info | ConvertTo-Json
}
"done in $([math]::Round(((Get-Date)-$t0).TotalMinutes,1)) min"
exit 0
