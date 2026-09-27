param([string]$Engine = 'I:\Program\Epic Games\UE_5.8', [switch]$NoInstall)
$ErrorActionPreference = 'Stop'
$pluginRoot = $PSScriptRoot
$projectRoot = Split-Path (Split-Path $pluginRoot -Parent) -Parent
# UAT removes the package directory. Never reuse a host that might contain a
# Content junction to user assets. This path is unique and must not exist.
$buildOutput = Join-Path $projectRoot ('Saved\VamBrowserBuild-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $buildOutput) { throw "Build output must not exist: $buildOutput" }
$ancestor = Get-Item -LiteralPath $projectRoot
while ($ancestor) {
    if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Build root traverses a reparse point: $($ancestor.FullName)" }
    $ancestor = $ancestor.Parent
}
$savedRoot = Join-Path $projectRoot 'Saved'
if ((Test-Path -LiteralPath $savedRoot) -and ((Get-Item -LiteralPath $savedRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Saved must not be a reparse point' }
$stage = Join-Path $projectRoot ('Saved\VamBrowserSource-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
# UAT copies the entire input directory before applying package filters. Never
# include live service locks, the resource index, or cached user data in it.
foreach ($entry in @('VamResourceBrowser.uplugin', 'Source', 'Config', 'Content', 'Web', 'Evidence', 'README.md', 'BREAST_JIGGLE.md', 'BREAST_CALIBRATION_RESEARCH.md', 'RunBreastJiggle.ps1', 'PHYSICS_RESET.md', 'STAGE02.md', 'STAGE03.md', 'STAGE04.md', 'STAGE05.md', 'STAGE06.md', 'STAGE07.md', 'SetupDecoder.ps1', 'RunStage07Regression.ps1', 'RunStage07Composition.ps1', 'RunStage07Cooked.ps1', 'BuildRuntimeConfiguration.ps1', 'Build.ps1')) {
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
