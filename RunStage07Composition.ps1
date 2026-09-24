param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Distribution,
    [Parameter(Mandatory=$true)][string[]]$BuildReports,
    [string]$Quality=(Join-Path $PSScriptRoot 'Config/Stage07CompositionQuality.json'),
    [int[]]$FrameRates=@(30,60,120),
    [switch]$Graphics
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Resolve-Path -LiteralPath $Project).Path -Parent
$distributionRoot=(Resolve-Path -LiteralPath $Distribution).Path
foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Source') -File -Recurse) {
    $relative=[IO.Path]::GetRelativePath($PSScriptRoot,$file.FullName)
    $copy=Join-Path $distributionRoot $relative
    if(!(Test-Path -LiteralPath $copy) -or (Get-FileHash $copy).Hash -ne (Get-FileHash $file.FullName).Hash) { throw ('Rebuild native distribution: '+$relative) }
}
$runId=[guid]::NewGuid().ToString('N')
$runRoot=Join-Path $PSScriptRoot ('Saved/Stage07/Composition-'+$runId)
$hostRoot=Join-Path $runRoot 'Host'
New-Item -ItemType Directory -Path (Join-Path $hostRoot 'Plugins') -Force | Out-Null
Copy-Item -LiteralPath $distributionRoot -Destination (Join-Path $hostRoot 'Plugins/VamResourceBrowser') -Recurse
New-Item -ItemType Junction -Path (Join-Path $hostRoot 'Content') -Target (Join-Path $projectRoot 'Content') | Out-Null
$hostProject=Join-Path $hostRoot 'Composition.uproject'
'{"FileVersion":3,"Plugins":[{"Name":"VamResourceBrowser","Enabled":true},{"Name":"PythonScriptPlugin","Enabled":true}]}' | Set-Content $hostProject -Encoding utf8
$spec=@{build_reports=@($BuildReports | ForEach-Object {(Resolve-Path -LiteralPath $_).Path});quality=(Resolve-Path $Quality).Path;map_root=('/Game/VamRuntimeTests/Run_'+$runId);shared_asset_instance=$true;warm_material_shaders=[bool]$Graphics}
$physicsQuality=Get-Content -LiteralPath $spec.quality -Raw | ConvertFrom-Json
if(!$physicsQuality.chaos_substepping -or $physicsQuality.chaos_max_substep_seconds -le 0 -or $physicsQuality.chaos_max_substeps -lt 1) {throw 'Explicit Chaos substep quality configuration required'}
$hostConfig=New-Item -ItemType Directory -Path (Join-Path $hostRoot 'Config') -Force
$stepText=([double]$physicsQuality.chaos_max_substep_seconds).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
@('[ /Script/Engine.PhysicsSettings]'.Replace('[ ','['),'bSubstepping=True','bTickPhysicsAsync=False',('MaxSubstepDeltaTime='+$stepText),('MaxSubsteps='+$physicsQuality.chaos_max_substeps)) | Set-Content (Join-Path $hostConfig.FullName 'DefaultEngine.ini') -Encoding utf8
if($Graphics) {
    @('[SystemSettings]','r.ShaderCompiler.JobCacheDDC=0') | Add-Content (Join-Path $hostConfig.FullName 'DefaultEngine.ini') -Encoding utf8
}
$specFile=Join-Path $runRoot 'spec.json';$mapReport=Join-Path $runRoot 'map.json'
$spec | ConvertTo-Json -Depth 10 | Set-Content $specFile -Encoding utf8
$oldSpec=$env:VAM_COMPOSITION_SPEC;$oldMap=$env:VAM_COMPOSITION_MAP_REPORT
try {
    $env:VAM_COMPOSITION_SPEC=$specFile;$env:VAM_COMPOSITION_MAP_REPORT=$mapReport
    $editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $arguments=@(('"'+$hostProject+'"'),'-run=pythonscript',('-script="'+$PSScriptRoot+'/Scripts/ue_stage07_composition_map.py"'),'-unattended','-nosplash','-NullRHI',('-abslog="'+$runRoot+'/map.log"'))
    if($Graphics) {$arguments=@($arguments | Where-Object {$_ -ne '-NullRHI'})+@('-AllowCommandletRendering')}
    $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $mapTimeout=if($Graphics){1200000}else{180000}
    if(!$process.WaitForExit($mapTimeout)) { Stop-Process -Id $process.Id;throw 'Composition map build timed out' }
    if($process.ExitCode -ne 0) {throw ('Composition map build failed: '+$runRoot+'/map.log')}
    $map=(Get-Content $mapReport -Raw | ConvertFrom-Json).map
    $runs=@()
    foreach($fps in $FrameRates) {
        if($fps -lt 1) {throw 'Frame rate must be positive'}
        $editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
        $arguments=@(('"'+$hostProject+'"'),$map,'-game','-NullRHI','-unattended','-nosplash','-VamStage07Composition',('-ExecCmds="t.MaxFPS '+$fps+'"'),('-abslog="'+$runRoot+'/game-'+$fps+'.log"'))
        if($Graphics) {$arguments=@($arguments | Where-Object {$_ -ne '-NullRHI'})+@('-windowed','-ResX=1280','-ResY=720','-VamStage07Screenshot')}
        $startedUtc=[DateTime]::UtcNow
        $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $timeout=if($Graphics){900000}else{120000}
        if(!$process.WaitForExit($timeout)) {Stop-Process -Id $process.Id;throw ('Play test timed out at '+$fps+' FPS')}
        if($process.ExitCode -ne 0) {throw ('Play test process failed at '+$fps+' FPS: '+$runRoot)}
        $resultFile=Get-Item -LiteralPath (Join-Path $hostRoot 'Saved/Stage07Composition.json')
        if($resultFile.LastWriteTimeUtc -lt $startedUtc) {throw 'Play process did not produce a fresh report'}
        $result=Get-Content -LiteralPath $resultFile.FullName -Raw | ConvertFrom-Json
        $result | Add-Member -NotePropertyName requested_render_fps -NotePropertyValue $fps
        $result | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $runRoot ('result-'+$fps+'.json')) -Encoding utf8
        if(!$result.composition_passed) {throw ('Play regression failed: '+$result.reason+'; '+$runRoot)}
        if($Graphics) {Copy-Item -LiteralPath (Join-Path $hostRoot 'Saved/Stage07Composition.png') -Destination (Join-Path $runRoot ('skin-'+$fps+'.png'))}
        $runs+=('result-'+$fps+'.json')
        Write-Output ('Passed Play regression at '+$fps+' FPS')
    }
    @{scope='composition/collision/ground/clock regression; not full Stage07';stage07_passed=$false;runs=$runs;map=$map;runtime_dll_sha256=(Get-FileHash (Join-Path $distributionRoot 'Binaries/Win64/UnrealEditor-VamCharacterRuntime.dll')).Hash} | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $runRoot 'summary.json') -Encoding utf8
    Write-Output ('Evidence: '+$runRoot)
}
finally {$env:VAM_COMPOSITION_SPEC=$oldSpec;$env:VAM_COMPOSITION_MAP_REPORT=$oldMap}
