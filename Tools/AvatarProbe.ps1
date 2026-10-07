# God of War Unity port step 0: fetch iltenahmet/god-of-war-unity (MIT) with curl (timeouts + retries) and extract ONLY
# what the port uses: Assets/Animations (Mixamo clips), Assets/Mutant (enemy textures), Assets/Scripts (reference for the
# port), Assets/Kratos + Assets/Axe (PRIVATE BUILD ONLY - not licensed by the repo), LICENSE, README.md.
. "$PSScriptRoot\Common.ps1"
$av=Join-Path $Work 'avatar'; New-Item -ItemType Directory -Force -Path $av | Out-Null
$zip=@((Join-Path $av 'god-of-war-unity-master.zip'), (Join-Path $av 'gowu-master-curl.zip')) | ? { Test-Path $_ } | Select -First 1
if(!$zip){
  $zip=Join-Path $av 'god-of-war-unity-master.zip'
  "download repo zip (curl, 30 s connect timeout, stall abort after 60 s below 1 KB/s, 5 retries)"
  & curl.exe -L --fail --retry 5 --retry-delay 5 --connect-timeout 30 --speed-limit 1024 --speed-time 60 -sS -o $zip 'https://codeload.github.com/iltenahmet/god-of-war-unity/zip/refs/heads/master'
  if($LASTEXITCODE -ne 0){ "download failed ($LASTEXITCODE)"; exit 1 }
}
"zip $((Get-Item $zip).Length) bytes sha256 $((Get-FileHash $zip -Algorithm SHA256).Hash)"
$ref=Join-Path $av 'ref'
if(Test-Path $ref){ Remove-Item $ref -Recurse -Force }
New-Item -ItemType Directory -Force -Path $ref | Out-Null
$ref=(Resolve-Path $ref).Path
Add-Type -AssemblyName System.IO.Compression.FileSystem
$keep='^god-of-war-unity-master/(Assets/(Animations|Mutant|Scripts|Kratos|Axe)/|LICENSE$|README\.md$)'
$z=[IO.Compression.ZipFile]::OpenRead($zip); $n=0
try {
  foreach($e in $z.Entries){
    if($e.FullName -notmatch $keep -or $e.FullName.EndsWith('/') -or $e.FullName.EndsWith('.meta')){ continue }
    $dest=[IO.Path]::GetFullPath((Join-Path $ref $e.FullName))
    if(-not $dest.StartsWith($ref+'\',[StringComparison]::OrdinalIgnoreCase)){ throw "entry escapes ref dir: $($e.FullName)" }
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    [IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dest,$true); $n++
  }
} finally { $z.Dispose() }
"extracted $n files"
$inv=Join-Path $Logs 'avatar-ref-inventory.txt'
Get-ChildItem $ref -Recurse -File | % { "{0}`t{1}" -f $_.Length, $_.FullName.Substring($ref.Length+1) } | Set-Content $inv
Get-Content $inv | % { "  $_" }
