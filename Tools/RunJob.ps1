$ErrorActionPreference='Stop'
$Root=Split-Path $PSScriptRoot -Parent
$Logs=Join-Path $Root 'Logs'; New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Job=Join-Path $Root 'Jobs\job.ps1'
$Status=Join-Path $Logs 'job-status.txt'
$LockPath=Join-Path $Logs 'job-runner.lock'
$Lock=$null
try { $Lock=[IO.File]::Open($LockPath,'OpenOrCreate','ReadWrite','None') } catch {
  # A job that has been 'running' for over 30 minutes is treated as hung (e.g. a stalled download whose window is
  # still open): run on a second lock instead of waiting forever.
  $st=Get-Item $Status -ErrorAction SilentlyContinue
  if($st -and ((Get-Date) - $st.LastWriteTime).TotalMinutes -gt 30){
    try { $Lock=[IO.File]::Open((Join-Path $Logs 'job-runner-b.lock'),'OpenOrCreate','ReadWrite','None'); Write-Host 'Previous job looks hung; running on the second lock.' } catch { }
  }
  if(-not $Lock){ Write-Host 'A job is already running.'; Start-Sleep 5; exit 1 }
}
try {
  # Apply a pending source sync (zip of changed project files) before running the job.
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  foreach($Sync in @(Get-ChildItem (Join-Path $Root 'Jobs') -Filter 'sync*.zip' | Sort-Object Name)){
    $z=[IO.Compression.ZipFile]::OpenRead($Sync.FullName)
    try {
      foreach($e in $z.Entries){
        if($e.FullName.EndsWith('/')){ continue }
        $dest=[IO.Path]::GetFullPath((Join-Path $Root $e.FullName))
        if(-not $dest.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){ throw "sync entry escapes project: $($e.FullName)" }
        New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($e,$dest,$true)
        # Fresh mtime so UBT/UHT always see synced sources as changed (zip times are zone-less).
        (Get-Item -LiteralPath $dest).LastWriteTime = Get-Date
      }
    } finally { $z.Dispose() }
    Add-Content (Join-Path $Logs 'sync.log') ("$((Get-Date).ToString('o')) applied $($Sync.Name) entries=$($z.Entries.Count)")
    Remove-Item $Sync.FullName -Force
  }
  if(!(Test-Path $Job)){ Set-Content $Status "no job"; exit 1 }
  $Id=(Get-Content (Join-Path $Root 'Jobs\job-id.txt') -ErrorAction SilentlyContinue | Select -First 1); if(!$Id){$Id='unnamed'}
  $Log=Join-Path $Logs ("job-$Id.log")
  Set-Content $Status ("running $Id " + (Get-Date).ToString('o'))
  $code=0
  try {
    $ErrorActionPreference='Continue'
    [IO.File]::WriteAllText($Log,'')
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Job 2>&1 | ForEach-Object { $s="$_"; Write-Host $s; [IO.File]::AppendAllText($Log,$s+"`r`n") }
    $code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
  } catch { $_ | Out-File -Append $Log; $code=99 }
  Set-Content $Status ("finished $Id exit=$code " + (Get-Date).ToString('o'))
} finally { $Lock.Dispose() }
Start-Sleep 3
