# Creates a desktop shortcut "Crossover-Rebuilt (Play)" that starts Minecraft + the Unreal game (Play.cmd).
. "$PSScriptRoot\Common.ps1"
$desk=[Environment]::GetFolderPath('Desktop')
$lnk=Join-Path $desk 'Crossover-Rebuilt (Play).lnk'
$sh=New-Object -ComObject WScript.Shell
$s=$sh.CreateShortcut($lnk)
$s.TargetPath=Join-Path $Root 'Play.cmd'
$s.WorkingDirectory=$Root
$s.WindowStyle=1  # normal window: shows progress/errors, closes itself when both are running
$ico=Join-Path $Root 'Package\WindowsNoEditor\CrossoverRebuilt\Binaries\Win64\CrossoverRebuilt.exe'
if(Test-Path $ico){ $s.IconLocation="$ico,0" }
$s.Description='Start Minecraft 1.20.1 (Fabric) and the Crossover-Rebuilt Unreal game'
$s.Save()
"shortcut: $lnk -> $($s.TargetPath)"
