# Physics & Portal mod: rig + animate the user's sculpted 3D Steve (Avatar/blender/pp_steve_build.py). Output: Work\pp\build.
. "$PSScriptRoot\Common.ps1"
$blender=Join-Path $Work 'tools\blender-3.6.23-windows-x64\blender.exe'
if(!(Test-Path $blender)){ "blender missing"; exit 1 }
$out=Join-Path $Work 'pp\build'
if(Test-Path $out){ Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$npz=Join-Path $Root 'Avatar\steve3d\steve3d_rest.npz'; $skin=Join-Path $Root 'Avatar\steve3d\T_Steve3D_Skin.png'
& $blender -b --factory-startup -P (Join-Path $Root 'Avatar\blender\pp_steve_build.py') -- $npz $skin $out 2>&1 | Select-String -Pattern 'STEVE3D|SKELETON|EXPORTED|WARN|DONE|Error|Traceback|line \d' | % { $_.Line }
if(!(Test-Path (Join-Path $out 'fbx\Steve3D.fbx'))){ "PP STEVE BUILD FAILED"; exit 2 }
$prev=Join-Path $Root 'Reports\pp-preview'; New-Item -ItemType Directory -Force -Path $prev | Out-Null
Get-ChildItem $out -Filter *.png | Copy-Item -Destination $prev -Force
Get-ChildItem $out -Recurse -File | % { "  $($_.FullName.Substring($out.Length+1)) $($_.Length)" }
exit 0
