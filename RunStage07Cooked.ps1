param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$CompositionRun,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$GraphicsQuality=(Join-Path $PSScriptRoot 'Config/Stage07GraphicsQuality.json'),
    [int[]]$FrameRates=@(30,60,120),
    [switch]$Graphics
)
$ErrorActionPreference='Stop'
$graphicsPolicy=Get-Content -LiteralPath $GraphicsQuality -Raw | ConvertFrom-Json
if($graphicsPolicy.schema -ne 'vam-graphics-quality/1') {throw 'Invalid graphics quality policy'}
$inputRoot=(Resolve-Path -LiteralPath $CompositionRun).Path
$hostRoot=Join-Path $inputRoot 'Host'
$project=Join-Path $hostRoot 'Composition.uproject'
$mapReport=Get-Content (Join-Path $inputRoot 'map.json') -Raw | ConvertFrom-Json
$summary=Get-Content (Join-Path $inputRoot 'summary.json') -Raw | ConvertFrom-Json
if($summary.map -ne $mapReport.map) {throw 'Composition map identity mismatch'}
$plugin=Join-Path $hostRoot 'Plugins/VamResourceBrowser'
foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Source') -File -Recurse) {
    $relative=[IO.Path]::GetRelativePath($PSScriptRoot,$file.FullName)
    $copy=Join-Path $plugin $relative
    if(!(Test-Path -LiteralPath $copy) -or (Get-FileHash $copy).Hash -ne (Get-FileHash $file.FullName).Hash) {throw ('Stale composition host: '+$relative)}
}
if((Get-FileHash (Join-Path $plugin 'Binaries/Win64/UnrealEditor-VamCharacterRuntime.dll')).Hash -ne $summary.runtime_dll_sha256) {throw 'Composition DLL changed'}
foreach($run in $summary.runs) {
    if(!(Get-Content (Join-Path $inputRoot $run) -Raw | ConvertFrom-Json).composition_passed) {throw 'Editor composition must pass before Cooked regression'}
}
$output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $output) {throw 'Use a new explicit output directory; previous Cooked evidence is protected'}
New-Item -ItemType Directory -Path $output | Out-Null
$archive=Join-Path $output 'Package'
$uat=Join-Path $Engine 'Engine/Build/BatchFiles/RunUAT.bat'
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $uat BuildCookRun "-project=$project" "-unrealexe=$editor" -noP4 -unattended -utf8output -nocompileeditor '-UbtArgs=-NoUBA' -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$archive" "-map=$($mapReport.map)" *> (Join-Path $output 'package.log')
if($LASTEXITCODE -ne 0) {throw ('Cook/package failed: '+$output+'/package.log')}
$exe=Join-Path $archive 'Windows/Composition/Binaries/Win64/Composition.exe'
if(!(Test-Path -LiteralPath $exe)) {throw 'Packaged launcher missing'}
$runs=@()
foreach($fps in $FrameRates) {
    if($fps -lt 1) {throw 'Frame rate must be positive'}
    $userDir=Join-Path $output ('Run-'+$fps)
    New-Item -ItemType Directory -Path $userDir | Out-Null
    $arguments=@($mapReport.map,'-unattended','-nosplash','-VamStage07Composition',('-UserDir="'+$userDir+'/"'),('-ExecCmds="t.MaxFPS '+$fps+'"'),('-abslog="'+$output+'/game-'+$fps+'.log"'))
    # UE 5.8's CSV processing thread reproduced a shutdown crash (777003) here.
    # Use the engine's synchronous collector and check the actual game exit code.
    if($Graphics) {$arguments+=('-csvCaptureFrames='+$graphicsPolicy.capture_frames);$arguments+='-csvCompression=0';$arguments+='-csvNoProcessingThread'}
    if($Graphics) {$arguments+=@('-windowed','-ResX=1280','-ResY=720','-VamStage07Screenshot')} else {$arguments+='-NullRHI'}
    $startedUtc=[DateTime]::UtcNow
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)) {Stop-Process -Id $process.Id;throw ('Cooked play timeout at '+$fps+' FPS')}
    if($process.ExitCode -ne 0) {throw ('Cooked process failed: '+$output)}
    $resultFile=Get-Item -LiteralPath (Join-Path $userDir 'Saved/Stage07Composition.json')
    if($resultFile.LastWriteTimeUtc -lt $startedUtc) {throw 'Missing fresh Cooked result'}
    $result=Get-Content $resultFile.FullName -Raw | ConvertFrom-Json
    if(!$result.composition_passed) {throw ('Cooked regression failed: '+$result.reason)}
    Copy-Item -LiteralPath $resultFile.FullName -Destination (Join-Path $output ('result-'+$fps+'.json'))
    if($Graphics) {
        Copy-Item -LiteralPath (Join-Path $userDir 'Saved/Stage07Composition.png') -Destination (Join-Path $output ('skin-'+$fps+'.png'))
        $captures=@(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved/Profiling/CSV') -Filter '*.csv' -File)
        if($captures.Count -ne 1) {throw 'Expected exactly one capture in the new isolated run'}
        $python=Join-Path $Engine 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
        & $python (Join-Path $PSScriptRoot 'Scripts/vam_graphics_evidence.py') --csv $captures[0].FullName --quality $GraphicsQuality --fps $fps --output (Join-Path $output ('pacing-'+$fps+'.json'))
        if($LASTEXITCODE -ne 0) {throw 'Rendered frame pacing validation failed'}
    }
    $runs+=('result-'+$fps+'.json')
    Write-Output ('Passed Cooked composition at '+$fps+' FPS')
}
$binary=Join-Path $archive 'Windows/Composition/Binaries/Win64/Composition.exe'
@{scope='Cooked animation/shape/rigid physics; not soft tissue';stage07_passed=$false;composition_run=$inputRoot;map=$mapReport.map;map_identity=$mapReport.identity;runs=$runs;graphics=[bool]$Graphics;executable_sha256=(Get-FileHash $binary).Hash} | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $output 'summary.json') -Encoding utf8
Write-Output ('Cooked evidence: '+$output)
