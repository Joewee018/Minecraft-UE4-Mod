. "$PSScriptRoot\Common.ps1"
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'Reports') | Out-Null
Set-Location (Join-Path $Root 'Bridge')
& .\gradlew.bat --no-daemon --console=plain liveProbe
"liveProbe exit $LASTEXITCODE"
