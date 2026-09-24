param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Distribution,
    [Parameter(Mandatory=$true)][string]$Recipe,
    [Parameter(Mandatory=$true)][string]$Report
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Resolve-Path -LiteralPath $Project).Path -Parent
$recipeFile=(Resolve-Path -LiteralPath $Recipe).Path
$reportFile=[System.IO.Path]::GetFullPath($Report)
$distributionRoot=(Resolve-Path -LiteralPath $Distribution).Path
# Reject stale native binaries/source bundles before opening the isolated build host.
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Source') -File -Recurse) {
    $relative=[System.IO.Path]::GetRelativePath($PSScriptRoot,$file.FullName)
    $copy=Join-Path $distributionRoot $relative
    if (-not (Test-Path -LiteralPath $copy) -or (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw ('Rebuild the native plugin distribution: '+$relative) }
}
$runRoot=Join-Path $PSScriptRoot ('Saved/Stage07/Build-'+[guid]::NewGuid().ToString('N'))
$hostRoot=Join-Path $runRoot 'Host'
New-Item -ItemType Directory -Path (Join-Path $hostRoot 'Plugins') -Force | Out-Null
Copy-Item -LiteralPath $distributionRoot -Destination (Join-Path $hostRoot 'Plugins/VamResourceBrowser') -Recurse
New-Item -ItemType Junction -Path (Join-Path $hostRoot 'Content') -Target (Join-Path $projectRoot 'Content') | Out-Null
$hostProject=Join-Path $hostRoot 'RuntimeBuild.uproject'
'{"FileVersion":3,"Plugins":[{"Name":"VamResourceBrowser","Enabled":true},{"Name":"PythonScriptPlugin","Enabled":true}]}' | Set-Content -LiteralPath $hostProject -Encoding utf8
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$script=Join-Path $PSScriptRoot 'Scripts/ue_runtime_build.py'
$priorRecipe=$env:VAM_RUNTIME_RECIPE; $priorReport=$env:VAM_RUNTIME_REPORT; $priorPhase=$env:VAM_RUNTIME_PHASE
try {
    $env:VAM_RUNTIME_RECIPE=$recipeFile; $env:VAM_RUNTIME_REPORT=$reportFile
    foreach ($phase in @('build','reload','verify')) {
        $env:VAM_RUNTIME_PHASE=$phase
        $log=Join-Path $runRoot ($phase+'.log')
        $arguments=@(('"'+$hostProject+'"'),'-run=pythonscript',('-script="'+$script+'"'),'-unattended','-nosplash','-NullRHI',('-abslog="'+$log+'"'))
        $process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(600000)) { Stop-Process -Id $process.Id; throw ('Runtime build timed out: '+$log) }
        if ($process.ExitCode -ne 0) { throw ('Runtime '+$phase+' failed; no publication. See '+$log) }
        $result=Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
        $expected=switch($phase) {build {'saved_pending_reload'} reload {'published_pending_verification'} verify {'committed'}}
        if ($result.status -ne $expected) { throw ('Unexpected transaction state: '+$result.status) }
    }
    Write-Output ('Committed runtime configuration: '+$result.configuration)
    Write-Output ('Receipt: '+$reportFile)
    Write-Output ('Independent process logs: '+$runRoot)
}
finally { $env:VAM_RUNTIME_RECIPE=$priorRecipe; $env:VAM_RUNTIME_REPORT=$priorReport; $env:VAM_RUNTIME_PHASE=$priorPhase }
