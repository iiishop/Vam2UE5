param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$RuntimeReport,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [switch]$Cook
)
$ErrorActionPreference='Stop'
$reportPath=(Resolve-Path -LiteralPath $RuntimeReport).Path
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if($report.status -ne 'committed') {throw 'A committed Runtime transaction is required'}
$output=[IO.Path]::GetFullPath($EvidenceRoot)
if(Test-Path -LiteralPath $output) {throw 'Choose a new evidence directory'}
New-Item -ItemType Directory -Path $output | Out-Null
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editor $Project '-ExecCmds=Automation RunTests Vam.Breast;Quit' '-TestExit=Automation Test Queue Empty' "-VamBreastTestConfig=$($report.configuration)" "-ReportExportPath=$output/Automation" -unattended -NullRHI -nosplash "-abslog=$output/tests.log" *> "$output/tests-console.log"
if($LASTEXITCODE -ne 0) {throw 'Breast engineering tests failed; inspect tests.log'}
$prior=$env:VAM_RUNTIME_REPORT
try {
    Copy-Item -LiteralPath $reportPath -Destination "$output/runtime.json"
    $env:VAM_RUNTIME_REPORT="$output/runtime.json"
    & $editor $Project '-run=pythonscript' "-script=$PSScriptRoot/Scripts/ue_breast_jiggle_audit.py" -unattended -NullRHI -nosplash "-abslog=$output/audit.log" *> "$output/audit-console.log"
    if($LASTEXITCODE -ne 0) {throw 'Breast asset audit failed'}
    if($Cook) {
        $audit=Get-Content "$output/audit.json" -Raw | ConvertFrom-Json
        & $editor $Project '-run=cook' '-TargetPlatform=Windows' "-Map=$($audit.map)" -unattended -NullRHI -nosplash "-abslog=$output/cook.log" *> "$output/cook-console.log"
        if($LASTEXITCODE -ne 0) {throw 'Cook failed'}
    }
} finally {$env:VAM_RUNTIME_REPORT=$prior}
Write-Output "Engineering evidence: $output. No visual acceptance is asserted."
