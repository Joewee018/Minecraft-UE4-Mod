# Builds and unit-tests the Fabric bridge. Output jar: Bridge\build\libs\crossover-rebuilt-bridge-<ver>.jar
. "$PSScriptRoot\Common.ps1"
Set-Location (Join-Path $Root 'Bridge')
& .\gradlew.bat --no-daemon --console=plain build
$code=$LASTEXITCODE
"gradle exit $code"
$jar=Get-ChildItem (Join-Path $Root 'Bridge\build\libs') -Filter 'crossover-rebuilt-bridge-*.jar' -ErrorAction SilentlyContinue | ? { $_.Name -notmatch 'sources' } | Select -First 1
if($jar){ $h=(Get-FileHash $jar.FullName -Algorithm SHA256).Hash; "bridge jar: $($jar.Name) sha256=$h"; Set-Content (Join-Path $Logs 'bridge-build.txt') "jar=$($jar.FullName)`r`nsha256=$h`r`nbuilt=$((Get-Date).ToString('o'))`r`nexit=$code" }
Get-ChildItem (Join-Path $Root 'Bridge\build\test-results\test') -Filter *.xml -ErrorAction SilentlyContinue | % { Copy-Item $_.FullName (Join-Path $Logs ('junit-' + $_.Name)) -Force }
exit $code
