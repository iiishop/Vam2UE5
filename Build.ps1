param([string]$Engine = 'I:\Program\Epic Games\UE_5.8', [switch]$NoInstall)
$ErrorActionPreference = 'Stop'
$pluginRoot = $PSScriptRoot
$projectRoot = Split-Path (Split-Path $pluginRoot -Parent) -Parent
$buildOutput = Join-Path $projectRoot 'Saved\VamBrowserBuild'
$stage = Join-Path $projectRoot ('Saved\VamBrowserSource-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
# UAT copies the entire input directory before applying package filters. Never
# include live service locks, the resource index, or cached user data in it.
foreach ($entry in @('VamResourceBrowser.uplugin', 'Source', 'Config', 'Content', 'Web', 'README.md', 'STAGE02.md', 'STAGE03.md', 'STAGE04.md', 'STAGE05.md', 'SetupDecoder.ps1', 'Build.ps1')) {
    Copy-Item -LiteralPath (Join-Path $pluginRoot $entry) -Destination $stage -Recurse
}
$scriptStage = New-Item -ItemType Directory -Path (Join-Path $stage 'Scripts')
Get-ChildItem -LiteralPath (Join-Path $pluginRoot 'Scripts') -File -Filter '*.py' | Copy-Item -Destination $scriptStage.FullName
$descriptor = Join-Path $stage 'VamResourceBrowser.uplugin'
& (Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat') BuildPlugin "-Plugin=$descriptor" "-Package=$buildOutput" -TargetPlatforms=Win64 -Rocket
if ($LASTEXITCODE -ne 0) { throw 'UE plugin build failed. See the concrete compiler/SDK/linker error in the UAT log above.' }
# BuildPlugin deliberately strips EnabledByDefault. Restore our explicit opt-in:
# otherwise a project plugin is also considered enabled in UBT's empty default
# project, and a Blueprint-only host can incorrectly reuse stock UnrealGame
# instead of generating the native target that links VamCharacterRuntime.
$packagedDescriptor = Join-Path $buildOutput 'VamResourceBrowser.uplugin'
$packagedJson = Get-Content -LiteralPath $packagedDescriptor -Raw | ConvertFrom-Json
$packagedJson | Add-Member -NotePropertyName EnabledByDefault -NotePropertyValue $false -Force
$packagedJson | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $packagedDescriptor -Encoding utf8
$builtBinaries = Join-Path $buildOutput 'Binaries'
if (-not (Test-Path -LiteralPath $builtBinaries)) { throw 'Build completed without plugin binaries.' }
if (-not $NoInstall) { Copy-Item -LiteralPath $builtBinaries -Destination $pluginRoot -Recurse -Force }
if ($NoInstall) { Write-Host "Plugin distribution built: $buildOutput (not installed)." }
else { Write-Host 'Plugin binaries installed. Restart UE, enable VaM Resource Browser, then open Window > VaM 资源浏览器.' }
