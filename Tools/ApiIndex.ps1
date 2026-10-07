# Writes declaration-only signature lines from Loom's generated (Mojang-mapped) 1.20.1 sources
# into Work\api-index for local reference. Not distributed.
. "$PSScriptRoot\Common.ps1"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Out=Join-Path $Work 'api-index'; New-Item -ItemType Directory -Force -Path $Out | Out-Null
$jars=@(Get-ChildItem -Path (Join-Path $Work 'gh'),(Join-Path $Root 'Bridge\.gradle') -Recurse -Filter '*-sources.jar' -ErrorAction SilentlyContinue | ? { $_.Name -match 'minecraft' })
$jars | % { "jar: $($_.FullName) $($_.Length)" } | Set-Content (Join-Path $Out '_jars.txt')
$classes=Get-Content (Join-Path $PSScriptRoot 'api-classes.txt') | ? { $_ -and -not $_.StartsWith('#') }
$n=0
foreach($jar in $jars){
  $n++; $short=Join-Path $Out ("src$n.jar")
  [IO.File]::Copy('\\?\'+$jar.FullName, $short, $true)
  $z=[IO.Compression.ZipFile]::OpenRead($short)
  try {
    foreach($c in $classes){
      $e=$z.GetEntry($c.Trim()); if(!$e){ continue }
      $r=New-Object IO.StreamReader($e.Open()); $text=$r.ReadToEnd(); $r.Close()
      $lines=$text -split "`n" | ? { $_ -match '^\s{0,8}(public|protected|private|static|final|abstract|class|interface|enum|record|default)\b' -and $_ -notmatch '^\s*(return|if|for|while)\b' } | % { $_.TrimEnd() }
      $name=($c.Trim() -replace '[/\\]','.')
      ($lines -join "`r`n") | Set-Content (Join-Path $Out ($name+'.txt'))
    }
  } finally { $z.Dispose() }
}
"index done: " + (Get-ChildItem $Out).Count
