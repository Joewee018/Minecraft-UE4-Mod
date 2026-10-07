# God of War Unity port: build the Unreal-ready FBX set (Mutant mesh + all clips on its skeleton) with Blender 3.6.
. "$PSScriptRoot\Common.ps1"
$blender=Join-Path $Work 'tools\blender-3.6.23-windows-x64\blender.exe'
if(!(Test-Path $blender)){ "blender missing"; exit 1 }
$ref=Join-Path $Work 'avatar\ref\god-of-war-unity-master'
if(!(Test-Path $ref)){ "reference repo not extracted"; exit 1 }
$out=Join-Path $Work 'avatar\build'
if(Test-Path $out){ Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $blender -b --factory-startup -P (Join-Path $Root 'Avatar\blender\gow_build.py') -- $ref $out 1.9 2>&1 | Select-String -Pattern 'KRATOS|AXE|MUTANT|SKELETON|EXPORTED|WARN|DONE|Error|Traceback|line \d' | % { $_.Line }
Get-ChildItem $out -Recurse -File | % { "  $($_.FullName.Substring($out.Length+1)) $($_.Length)" }
if(!(Test-Path (Join-Path $out 'fbx\Avatar.fbx'))){ "AVATAR BUILD FAILED"; exit 2 }
exit 0
