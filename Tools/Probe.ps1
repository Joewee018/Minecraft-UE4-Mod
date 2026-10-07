$ErrorActionPreference='Continue'
$Root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'Logs') | Out-Null
$Out=Join-Path $Root 'Logs\probe.txt'
function W($s){ Add-Content -LiteralPath $Out -Value $s }
Set-Content -LiteralPath $Out -Value ("probe " + (Get-Date).ToString('o'))
W ("OS: " + [Environment]::OSVersion.VersionString)
W ("CPU: " + (Get-CimInstance Win32_Processor | Select -First 1).Name + " cores=" + [Environment]::ProcessorCount)
W ("RAM GB: " + [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1))
foreach($u in 'C:\Program Files\Epic Games\UE_4.27','C:\Epic Games\UE_4.27'){
  $bv=Join-Path $u 'Engine\Build\Build.version'
  if(Test-Path $bv){ W "UE: $u"; W (Get-Content $bv -Raw) }
}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if(Test-Path $vswhere){ W "VS:"; & $vswhere -all -products * -format text -property installationPath | % { W $_ }; & $vswhere -all -products * -property catalog_productDisplayVersion | % { W $_ } }
Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -ErrorAction SilentlyContinue | % { W ("WinSDK: " + $_.Name) }
Get-ChildItem 'C:\Program Files\Microsoft','C:\Program Files\Java','C:\Program Files\Eclipse Adoptium' -Directory -ErrorAction SilentlyContinue | ? { $_.Name -like 'jdk*' } | % { W ("JDK dir: " + $_.FullName) }
$j='C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot\bin\java.exe'
if(Test-Path $j){ W ("java17: " + ((& $j -version 2>&1) -join ' | ')) }
foreach($url in 'https://maven.fabricmc.net/net/fabricmc/fabric-loader/maven-metadata.xml','https://piston-meta.mojang.com/mc/game/version_manifest_v2.json','https://services.gradle.org/distributions/','https://libraries.minecraft.net/'){
  try { $r=Invoke-WebRequest -UseBasicParsing -Method Head -Uri $url -TimeoutSec 15; W "net OK $($r.StatusCode) $url" } catch { W "net FAIL $url : $($_.Exception.Message)" }
}
W "Processes (java/Crossover):"
Get-CimInstance Win32_Process | ? { $_.Name -match '^(java|javaw|Crossover|CrossoverRebuilt)' } | % { $c=$_.CommandLine; if($c.Length -gt 400){$c=$c.Substring(0,400)}; W ("  pid=$($_.ProcessId) $($_.Name) :: $c") }
Get-PSDrive -PSProvider FileSystem | % { W ("drive $($_.Name) freeGB=" + [math]::Round($_.Free/1GB,1)) }
$loom=Join-Path $env:USERPROFILE '.gradle\caches\fabric-loom'
if(Test-Path $loom){ W "loom cache:"; Get-ChildItem $loom -Recurse -Filter '*sources*.jar' -ErrorAction SilentlyContinue | Select -First 20 | % { W ("  " + $_.FullName + " " + $_.Length) } }
W "done"
