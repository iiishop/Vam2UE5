param(
    [Parameter(Mandatory=$true)][string]$Job,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Engine='I:\Program\Epic Games\UE_5.8'
)
$ErrorActionPreference='Stop'
$jobDirectory=(Resolve-Path -LiteralPath $Job).Path
$recipe=Get-Content (Join-Path $jobDirectory 'recipe.json') -Raw | ConvertFrom-Json
$verified=Get-Content (Join-Path $jobDirectory 'assembly-verified.json') -Raw | ConvertFrom-Json
if (!$verified.assembly.valid -or $recipe.state -ne 'VerifiedEditorAssembly') {throw 'Assembly must pass independent verification before cooking.'}
$output=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) {throw 'Use a new explicit output directory; existing evidence is preserved.'}
# UBT still rejects long generated compiler paths on this Windows toolchain.
# Catch the problem before copying the verified dependency closure.
foreach ($sourceFile in (Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Source') -Filter '*.cpp' -Recurse -File)) {
    $sourceRoot=(Join-Path $PSScriptRoot 'Source').TrimEnd('/','\')
    $relativeSource=$sourceFile.FullName.Substring($sourceRoot.Length+1)
    $moduleName=($relativeSource -split '[/\\]')[0]
    $generatedPath=Join-Path $output ('Host/Plugins/VamResourceBrowser/Intermediate/Build/Win64/x64/UnrealGame/Development/'+$moduleName+'/'+$sourceFile.Name+'.dep.json')
    if($generatedPath.Length -ge 260) {throw 'OutputRootTooLong: choose a shorter explicit path, for example the project Saved/MHFaceCook directory. Nothing has been copied.'}
}
$hostDirectory=Join-Path $output 'Host'
$plugin=Join-Path $hostDirectory 'Plugins/VamResourceBrowser'
New-Item -ItemType Directory -Path $plugin -Force | Out-Null
foreach ($directory in @('Source','Binaries','Resources','Content','Config')) {
    if(Test-Path (Join-Path $PSScriptRoot $directory)) {Copy-Item -LiteralPath (Join-Path $PSScriptRoot $directory) -Destination $plugin -Recurse}
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'VamResourceBrowser.uplugin') -Destination $plugin
$sourceProject=Split-Path $recipe.project -Parent
foreach ($package in $verified.dependency_closure) {
    if (!$package.StartsWith('/Game/')) {continue}
    $relative='Content/'+$package.Substring(6)+'.uasset'
    $source=Join-Path $sourceProject $relative
    if (!(Test-Path -LiteralPath $source)) {throw "Missing verified package $package"}
    if ((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -cne $verified.game_package_sha256.$package) {throw "Verified package changed: $package"}
    $destination=Join-Path $hostDirectory $relative
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    foreach ($extension in @('.ubulk','.uexp','.uptnl')) {
        $sidecar=[IO.Path]::ChangeExtension($source,$extension)
        if(Test-Path -LiteralPath $sidecar) {Copy-Item -LiteralPath $sidecar -Destination ([IO.Path]::ChangeExtension($destination,$extension))}
    }
}
$project=Join-Path $hostDirectory 'MH00Cook.uproject'
@{FileVersion=3;EngineAssociation='5.8';Plugins=@(@{Name='VamResourceBrowser';Enabled=$true})} | ConvertTo-Json -Depth 5 | Set-Content $project -Encoding utf8
New-Item -ItemType Directory -Path (Join-Path $hostDirectory 'Config') -Force | Out-Null
$engineConfig=Get-Content (Join-Path $sourceProject 'Config/DefaultEngine.ini') -Raw
$engineConfig=[regex]::Replace($engineConfig,'(?m)^(EditorStartupMap|GameDefaultMap)=.*$','$1=/Game/MH00Verification/L_Empty')
$engineConfig=[regex]::Replace($engineConfig,'(?m)^GlobalDefaultGameMode=.*$','GlobalDefaultGameMode=/Script/Engine.GameModeBase')
$engineConfig | Set-Content (Join-Path $hostDirectory 'Config/DefaultEngine.ini') -Encoding utf8
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editor $project '-run=pythonscript' "-script=$PSScriptRoot/Scripts/ue_metahuman_empty_map.py" -NullRHI -unattended -nosplash "-abslog=$output/create-map.log" *> (Join-Path $output 'create-map-console.log')
if($LASTEXITCODE -ne 0) {throw 'Empty map creation failed'}
$archive=Join-Path $output 'Package'
$cookDirectory=Join-Path $hostDirectory ('Content/'+$recipe.assets.assembly.Substring(6))
& (Join-Path $Engine 'Engine/Build/BatchFiles/RunUAT.bat') BuildCookRun "-project=$project" "-unrealexe=$editor" -noP4 -unattended -utf8output -nocompileeditor '-UbtArgs=-NoUBA' '-AdditionalCookerOptions=-SkipZenStore' -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$archive" '-map=/Game/MH00Verification/L_Empty' "-CookDir=$cookDirectory" *> (Join-Path $output 'package.log')
if($LASTEXITCODE -ne 0) {throw 'Cook/package failed; see package.log'}
$executables=@(Get-ChildItem -LiteralPath (Join-Path $archive 'Windows') -Filter 'MH00Cook.exe' -Recurse -File | Where-Object {$_.FullName -match 'Binaries'})
if($executables.Count -ne 1) {throw 'Packaged executable missing or ambiguous'}
$runDirectory=Join-Path $output 'Run'
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$arguments=@('/Game/MH00Verification/L_Empty','-NullRHI','-unattended','-nosplash',('-UserDir="'+$runDirectory+'/"'),('-abslog="'+$output+'/game.log"'),('-ExecCmds="vam.MetaHuman.Verify '+$recipe.blueprint+'_C"'))
$process=Start-Process -FilePath $executables[0].FullName -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(180000)) {Stop-Process -Id $process.Id;throw 'Cooked lifecycle timed out'}
if($process.ExitCode -ne 0) {throw 'Cooked lifecycle process failed'}
$result=Get-Content (Join-Path $runDirectory 'Saved/MetaHumanLifecycle.json') -Raw | ConvertFrom-Json
if(!$result.passed -or !$result.cooked) {throw 'Cooked lifecycle check did not pass'}
$result | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $output 'result.json') -Encoding utf8
Write-Output "Cooked lifecycle passed: $output/result.json"
