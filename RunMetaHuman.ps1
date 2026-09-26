param(
    [Parameter(Mandatory=$true)][string]$Job,
    [ValidateSet('resume','rig','adopt-calibration','fit')][string]$Mode='resume',
    [string]$Engine='I:\Program\Epic Games\UE_5.8',
    [string]$Project=(Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'SmartNPC.uproject'),
    [switch]$ResumeCancelled
)
$ErrorActionPreference='Stop'
$jobDirectory=(Resolve-Path -LiteralPath $Job).Path
if (-not (Test-Path -LiteralPath (Join-Path $jobDirectory 'recipe.json')) -and -not (Test-Path -LiteralPath (Join-Path $jobDirectory 'request.json'))) { throw 'Job must contain a request or recipe.' }
if ($ResumeCancelled) {
    $cancelFile=Join-Path $jobDirectory 'cancel'
    if (Test-Path -LiteralPath $cancelFile) { Remove-Item -LiteralPath $cancelFile }
}
if ($Mode -eq 'rig') {
    $disclosure=Join-Path $jobDirectory 'upload-disclosure.json'
    if (-not (Test-Path -LiteralPath $disclosure)) { throw 'Run resume first to prepare the exact upload disclosure.' }
    Get-Content -LiteralPath $disclosure -Raw | Write-Host
    $consentPath=Join-Path $PSScriptRoot 'Saved/MetaHuman/cloud-consent.json'
    $hasStandingConsent=$false
    if (Test-Path -LiteralPath $consentPath) {
        $consent=Get-Content -LiteralPath $consentPath -Raw | ConvertFrom-Json
        $requestScope=Get-Content -LiteralPath $disclosure -Raw | ConvertFrom-Json
        $hasStandingConsent=($consent.granted -eq $true -and [bool]$consent.user_instruction)
        foreach ($key in @('schema','recipient','operation','fields_from_local_service_implementation','excluded','texture_download')) {
            if (($consent.scope.$key | ConvertTo-Json -Compress -Depth 10) -cne ($requestScope.$key | ConvertTo-Json -Compress -Depth 10)) { $hasStandingConsent=$false }
        }
    }
    if ($hasStandingConsent) { Write-Host 'Using existing explicit authorization for this Epic service and data scope.' }
    else {
        $answer=Read-Host 'Authorize this Epic AutoRig upload and official texture-source requests? Type AUTHORIZE to proceed'
        if ($answer -cne 'AUTHORIZE') { throw 'Upload not authorized. Draft retained.' }
    }
    $previousGrant=$env:VAM_MH_AUTHORIZED_UPLOAD_SHA256
    $env:VAM_MH_AUTHORIZED_UPLOAD_SHA256=(Get-FileHash -LiteralPath $disclosure -Algorithm SHA256).Hash.ToLowerInvariant()
}
try {
    & (Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $Project '-run=pythonscript' "-script=$PSScriptRoot\Scripts\ue_metahuman_job.py" "-VamMHJob=$jobDirectory" "-VamMHMode=$Mode" '-AllowCommandletRendering' '-ini:Engine:[ConsoleVariables]:TextureGraph.AllowCommandlets=1' '-unattended' '-nosplash' "-abslog=$jobDirectory\resume.log"
    if ($LASTEXITCODE -ne 0) { throw "MH task failed; Draft retained. See $jobDirectory\status.json and resume.log" }
    Get-Content -LiteralPath (Join-Path $jobDirectory 'status.json') -Raw | Write-Host
} finally {
    if ($Mode -eq 'rig') { $env:VAM_MH_AUTHORIZED_UPLOAD_SHA256=$previousGrant }
}
