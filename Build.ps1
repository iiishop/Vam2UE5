param([string]$Engine = 'I:\Program\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$pluginRoot = $PSScriptRoot
$projectRoot = Split-Path (Split-Path $pluginRoot -Parent) -Parent
$buildOutput = Join-Path $projectRoot 'Saved\VamBrowserBuild'
$descriptor = Join-Path $pluginRoot 'VamResourceBrowser.uplugin'
& (Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat') BuildPlugin "-Plugin=$descriptor" "-Package=$buildOutput" -TargetPlatforms=Win64 -Rocket
if ($LASTEXITCODE -ne 0) { throw 'UE plugin compilation failed. Check the UAT log above; C++ Build Tools and Windows SDK are required.' }
$builtBinaries = Join-Path $buildOutput 'Binaries'
if (-not (Test-Path -LiteralPath $builtBinaries)) { throw 'Build completed without plugin binaries.' }
Copy-Item -LiteralPath $builtBinaries -Destination $pluginRoot -Recurse -Force
Write-Host 'Plugin binaries installed. Restart UE, enable VaM Resource Browser, then open Window > VaM 资源浏览器.'
