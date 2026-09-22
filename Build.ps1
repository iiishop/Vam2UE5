param([string]$Engine = 'I:\Program\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$pluginRoot = $PSScriptRoot
$projectRoot = Split-Path (Split-Path $pluginRoot -Parent) -Parent
$buildOutput = Join-Path $projectRoot 'Saved\VamBrowserBuild'
$stage = Join-Path $projectRoot ('Saved\VamBrowserSource-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
# UAT copies the entire input directory before applying package filters. Never
# include live service locks, the resource index, or cached user data in it.
foreach ($entry in @('VamResourceBrowser.uplugin', 'Source', 'Config', 'Scripts', 'Web', 'README.md', 'STAGE02.md', 'STAGE03.md', 'STAGE04.md', 'SetupDecoder.ps1')) {
    Copy-Item -LiteralPath (Join-Path $pluginRoot $entry) -Destination $stage -Recurse
}
$descriptor = Join-Path $stage 'VamResourceBrowser.uplugin'
& (Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat') BuildPlugin "-Plugin=$descriptor" "-Package=$buildOutput" -TargetPlatforms=Win64 -Rocket
if ($LASTEXITCODE -ne 0) { throw 'UE plugin compilation failed. Check the UAT log above; C++ Build Tools and Windows SDK are required.' }
$builtBinaries = Join-Path $buildOutput 'Binaries'
if (-not (Test-Path -LiteralPath $builtBinaries)) { throw 'Build completed without plugin binaries.' }
Copy-Item -LiteralPath $builtBinaries -Destination $pluginRoot -Recurse -Force
Write-Host 'Plugin binaries installed. Restart UE, enable VaM Resource Browser, then open Window > VaM 资源浏览器.'
