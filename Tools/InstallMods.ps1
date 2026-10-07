# Installs third-party mods for the DEV client only (Work\mc\mods). Never copied into Package\.
# Each entry is pinned by Modrinth CDN URL + SHA-512; a mismatch deletes the file and fails.
. "$PSScriptRoot\Common.ps1"
$mods=Join-Path $Work 'mc\mods'; New-Item -ItemType Directory -Force -Path $mods | Out-Null
$pins=@(
  @{ Name='From-The-Fog-1.20-v1.9.2-Forge-Fabric.jar'; Url='https://cdn.modrinth.com/data/p1WH6sHr/versions/JTd3TFcc/From-The-Fog-1.20-v1.9.2-Forge-Fabric.jar';
     Sha512='918386274b05335b4fcb0f8f42e8a2b5bdff699853fe39cc41709adff1aedbec151e1ab90ec2158db8e4aba89e4130bc9f6a4f0fe956fe155c8c970c1f8e341d' }
)
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$fail=0
foreach($m in $pins){
  $dest=Join-Path $mods $m.Name
  if(!(Test-Path $dest)){ "download $($m.Url)"; Invoke-WebRequest -Uri $m.Url -OutFile $dest -UseBasicParsing }
  $h=(Get-FileHash $dest -Algorithm SHA512).Hash.ToLower()
  if($h -ne $m.Sha512){ "SHA-512 MISMATCH $($m.Name): $h"; Remove-Item $dest -Force; $fail=1 } else { "ok $($m.Name) $((Get-Item $dest).Length) bytes sha512 verified" }
}
# Copy for inspection (cloud side reads it from Logs\inspect\)
$insp=Join-Path $Logs 'inspect'; New-Item -ItemType Directory -Force -Path $insp | Out-Null
foreach($m in $pins){ $d=Join-Path $mods $m.Name; if(Test-Path $d){ Copy-Item $d (Join-Path $insp $m.Name) -Force } }
exit $fail
