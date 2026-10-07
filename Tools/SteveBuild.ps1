# SM64 Steve Movement: build the Steve rig + original clips (Avatar/blender/steve_build.py) with Blender 3.6.
. "$PSScriptRoot\Common.ps1"
$blender=Join-Path $Work 'tools\blender-3.6.23-windows-x64\blender.exe'
if(!(Test-Path $blender)){ "blender missing"; exit 1 }
$out=Join-Path $Work 'steve\build'
if(Test-Path $out){ Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
# Steve's skin from the local Minecraft client jar (private build; never distributed).
$skin=Join-Path $Work 'steve\steve.png'
$jar=Join-Path $Work 'gh\caches\fabric-loom\1.20.1\minecraft-client.jar'
if(Test-Path $jar){
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $z=[IO.Compression.ZipFile]::OpenRead($jar)
  try { $e=$z.GetEntry('assets/minecraft/textures/entity/player/wide/steve.png'); if($e){ [IO.Compression.ZipFileExtensions]::ExtractToFile($e,$skin,$true); "steve skin extracted from $jar" } else { "WARN steve.png not in jar" } } finally { $z.Dispose() }
} else { "WARN minecraft client jar not found: $jar" }
& $blender -b --factory-startup -P (Join-Path $Root 'Avatar\blender\steve_build.py') -- $out $skin 2>&1 | Select-String -Pattern 'STEVE|SKELETON|EXPORTED|WARN|DONE|Error|Traceback|line \d' | % { $_.Line }
Get-ChildItem $out -Recurse -File | % { "  $($_.FullName.Substring($out.Length+1)) $($_.Length)" }
if(!(Test-Path (Join-Path $out 'fbx\Steve.fbx'))){ "STEVE BUILD FAILED"; exit 2 }
exit 0
