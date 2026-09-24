param(
    [Parameter(Mandatory=$true)][ValidateSet('Baseline','Flesh')][string]$Case,
    [ValidateSet(30,60,120)][int]$FrameRate=60,
    [string]$ProjectRoot=(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$Manifest=(Join-Path $PSScriptRoot 'Config/Examples/Stage07Replay.json'),
    [switch]$ShowWindow,
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if($config.schema -ne 'vam-stage07-replay/1') {throw 'Unsupported replay manifest'}
$entry=$config.$Case
$package=Join-Path $ProjectRoot $entry.package
$exe=Join-Path $package $entry.executable
if(!(Test-Path -LiteralPath $exe)) {throw "Recorded package is missing: $exe"}
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $entry.sha256) {throw 'Executable differs from recorded checkpoint'}
if($CheckOnly) {Write-Output "Verified $Case executable: $exe";return}
$run=Join-Path $PSScriptRoot ('Saved/Stage07Manual/'+$Case+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$arguments=@($entry.map,$entry.flag,'-windowed','-ResX=1280','-ResY=720',('-ExecCmds="t.MaxFPS '+$FrameRate+'"'),('-UserDir="'+$run+'/"'),('-abslog="'+$run+'/game.log"'))
if($Case -eq 'Baseline') {$arguments+='-VamStage07Screenshot'}
$style=if($ShowWindow){'Normal'}else{'Hidden'}
Write-Output "Replaying saved $Case checkpoint. Output: $run"
Write-Output 'This does not build or validate the latest source checkout. FrameRate is a cap, not a measured rate.'
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle $style -PassThru
if(!$process.WaitForExit(180000)) {Stop-Process -Id $process.Id;throw "Replay timed out. See $run"}
if($process.ExitCode -ne 0) {throw "Game exited with $($process.ExitCode). See $run"}
$resultFile=Join-Path $run ('Saved/'+$entry.report)
if(!(Test-Path -LiteralPath $resultFile)) {throw "Fresh report missing. See $run"}
$result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
if($Case -eq 'Baseline' -and (!$result.composition_passed -or !$result.timing_motion_settle_passed)) {throw "Baseline failed: $($result.reason). See $run"}
if($Case -eq 'Flesh') {
    if($result.reason -ne 'capture_complete_pending_visual_and_contact_review' -or $result.measured_frames -le 0) {throw "Flesh capture failed: $($result.reason). See $run"}
    foreach($index in 0..2) {
        if(!(Test-Path -LiteralPath (Join-Path $run "Saved/FleshCapability-$index.png"))) {throw 'Missing Flesh screenshot'}
    }
    $errors=Select-String -LiteralPath (Join-Path $run 'game.log') -Pattern 'Failed to find tet mesh index|missing usage flag MeshDeformer|LogFleshDeformer: Error'
    if($errors) {throw "Flesh rendering setup failed. See $run/game.log"}
}
@{case=$Case;executable_sha256=$entry.sha256;requested_fps_cap=$FrameRate;exit_code=$process.ExitCode;stage07_passed=$false;report=$resultFile;scope='Replay of recorded checkpoint, not latest-source or complete Stage07 validation'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'replay.json') -Encoding utf8
Write-Output "Replay completed: $resultFile"
if($Case -eq 'Flesh') {Write-Output 'Capture completed only. backend_qualified=false and stage07_passed=false are expected.'}
