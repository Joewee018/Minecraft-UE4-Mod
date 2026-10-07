# Installs the pinned portable Blender 3.6.23 LTS into Work\tools\blender (project-local; nothing system-wide).
. "$PSScriptRoot\Common.ps1"
$tools=Join-Path $Work 'tools'; New-Item -ItemType Directory -Force -Path $tools | Out-Null
$exe=Join-Path $tools 'blender-3.6.23-windows-x64\blender.exe'
if(Test-Path $exe){ "blender present: $exe"; exit 0 }
$zip=Join-Path $tools 'blender-3.6.23-windows-x64.zip'
$want='e3296eba7eab32c2e5182459ec7614af32224eee2bd32c9d0a08ffd751c54f3b'
if(!(Test-Path $zip) -or (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $want){
  "download blender-3.6.23-windows-x64.zip (388 MB)"
  & curl.exe -L --fail --retry 3 -sS -o $zip 'https://download.blender.org/release/Blender3.6/blender-3.6.23-windows-x64.zip'
}
$h=(Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if($h -ne $want){ "SHA-256 MISMATCH $h"; Remove-Item $zip -Force; exit 1 }
"sha256 verified $h"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip,$tools)
if(Test-Path $exe){ "installed $exe"; exit 0 } else { "extract failed"; exit 1 }
