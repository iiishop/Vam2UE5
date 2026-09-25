param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$Project,
    [Parameter(Mandatory=$true)][string]$MapsReport,
    [ValidateSet(30,60,120)][int]$FrameRate=60,
    [switch]$NullRHI,
    [switch]$ShowWindow
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $Executable).Path
$maps=Get-Content -LiteralPath $MapsReport -Raw | ConvertFrom-Json
if(!$maps.dependency_direction_passed -or $maps.manual_map_has_test_probe) {throw 'Fixture dependency checks failed'}
$run=Join-Path $PSScriptRoot ('Saved/Stage071/Audit-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
foreach($map in @($maps.maps | Select-Object -Skip 1)) {
    $name=($map -split '/')[-1]
    $out=Join-Path $run $name
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $arguments=@()
    if($Project) {$arguments+=('"'+(Resolve-Path -LiteralPath $Project).Path+'"')}
    $arguments+=@($map,'-game','-VamSoftTissueAudit','-unattended','-nosplash','-csvNoProcessingThread','-windowed','-ResX=1280','-ResY=720',('-ExecCmds="t.MaxFPS '+$FrameRate+'"'),('-UserDir="'+$out+'/"'),('-abslog="'+$out+'/game.log"'))
    if($NullRHI) {$arguments+='-NullRHI'}
    $style=if($ShowWindow){'Normal'}else{'Hidden'}
    Write-Output ('Testing '+$map+' -> '+$out)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle $style -PassThru
    if(!$process.WaitForExit(600000)) {Stop-Process -Id $process.Id;throw ('Audit timeout: '+$out)}
    if($process.ExitCode -ne 0) {throw ('Runtime exited '+$process.ExitCode+': '+$out)}
    $report=Join-Path $out 'Saved/SoftTissueArchitecture.json'
    if(!(Test-Path -LiteralPath $report)) {throw ('Fresh report absent: '+$out)}
    $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if(!$result.architecture_passed) {throw ('Lifecycle audit failed: '+$result.reason+'; '+$out)}
}
@{executable_sha256=(Get-FileHash -LiteralPath $exe).Hash;maps_report_sha256=(Get-FileHash -LiteralPath $MapsReport).Hash;requested_fps_cap=$FrameRate;null_rhi=$NullRHI.IsPresent;architecture_passed=$true;visual_acceptance_passed=$false} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'audit.json') -Encoding utf8
Write-Output ('Lifecycle audit complete; visual acceptance remains manual: '+$run)
