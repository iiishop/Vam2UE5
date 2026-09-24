param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Distribution,
    [string]$ProbeConfig = (Join-Path $PSScriptRoot 'Config/Examples/Stage07TransactionProbe.json')
)
$ErrorActionPreference = 'Stop'
$projectFile = (Resolve-Path -LiteralPath $Project).Path
$projectRoot = Split-Path $projectFile -Parent
$distributionRoot = (Resolve-Path -LiteralPath $Distribution).Path
$editor = Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $editor)) { throw 'UnrealEditor.exe missing' }
if (-not (Test-Path -LiteralPath (Join-Path $distributionRoot 'Binaries/Win64/UnrealEditor-VamCharacterRuntime.dll'))) { throw 'Build the plugin distribution first with Build.ps1 -NoInstall' }
$runRoot = Join-Path $PSScriptRoot ('Saved/Stage07/Run-' + [guid]::NewGuid().ToString('N'))
$hostRoot = Join-Path $runRoot 'Host'
$hostPlugins = New-Item -ItemType Directory -Path (Join-Path $hostRoot 'Plugins') -Force
$isolatedPlugin = Join-Path $hostPlugins.FullName 'VamResourceBrowser'
Copy-Item -LiteralPath $distributionRoot -Destination $isolatedPlugin -Recurse
# Read-only tests load the actual project packages through this link. No package
# save operation is performed by either probe. The host is retained for audit.
New-Item -ItemType Junction -Path (Join-Path $hostRoot 'Content') -Target (Join-Path $projectRoot 'Content') | Out-Null
$hostProject = Join-Path $hostRoot 'Stage07Regression.uproject'
'{"FileVersion":3,"Plugins":[{"Name":"VamResourceBrowser","Enabled":true},{"Name":"PythonScriptPlugin","Enabled":true}]}' | Set-Content -LiteralPath $hostProject -Encoding utf8
$config = Get-Content -LiteralPath $ProbeConfig -Raw | ConvertFrom-Json
$config | Add-Member -NotePropertyName require_fixed -NotePropertyValue $true -Force
$configFile = Join-Path $runRoot 'probe-config.json'
$config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configFile -Encoding utf8
$reportFile = Join-Path $runRoot 'transaction.json'

function Invoke-Probe([string]$Script, [string]$Log) {
    $arguments = @(('"' + $hostProject + '"'), ('-ExecutePythonScript="' + $Script + '"'), '-unattended', '-nosplash', '-NullRHI', ('-abslog="' + $Log + '"'))
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(180000)) {
        # This is only the process started by this invocation, never the user's editor.
        Stop-Process -Id $process.Id
        throw ('Probe timed out: ' + $Log)
    }
    if ($process.ExitCode -ne 0) { throw ('Editor failed: ' + $Log) }
}

$priorConfig = $env:VAM_STAGE07_PROBE_CONFIG
$priorReport = $env:VAM_STAGE07_PROBE_REPORT
$priorNative = $env:VAM_NATIVE_REPORT_FILE
try {
    $env:VAM_STAGE07_PROBE_CONFIG = $configFile
    $env:VAM_STAGE07_PROBE_REPORT = $reportFile
    Invoke-Probe (Join-Path $PSScriptRoot 'Scripts/ue_stage07_transaction_probe.py') (Join-Path $runRoot 'transaction.log')
    $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
    if (-not $report.regression_passed -or $report.status -ne 'observed') { throw ('Transaction regression failed: ' + $reportFile) }
    # Verify the sources actually compiled into this isolated distribution match
    # this checkout; a successful old DLL test is not current-code evidence.
    foreach ($property in $report.code_and_binary_sha256.PSObject.Properties) {
        if ($property.Name.StartsWith('Source')) {
            $actual = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $property.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $property.Value) { throw ('Distribution contains stale source: ' + $property.Name) }
        }
    }
    $definition = $report.definition.Split('.')[0]
    $folder = $definition.Substring(0, $definition.LastIndexOf('/'))
    $kernelConfig = Join-Path $runRoot 'shape-config.json'
    @{definition=$definition;folder=$folder;blueprint=($folder + '/BP_VamCharacter')} | ConvertTo-Json | Set-Content -LiteralPath $kernelConfig -Encoding utf8
    $kernelOutput = Join-Path $isolatedPlugin 'Saved/NativeBuild'
    New-Item -ItemType Directory -Path $kernelOutput -Force | Out-Null
    $env:VAM_NATIVE_REPORT_FILE = $kernelConfig
    Invoke-Probe (Join-Path $isolatedPlugin 'Scripts/ue_shape_kernel_check.py') (Join-Path $runRoot 'shape-kernel.log')
    $kernelFile = Join-Path $kernelOutput 'shape-kernel-runtime-check.json'
    $kernel = Get-Content -LiteralPath $kernelFile -Raw | ConvertFrom-Json
    if ($kernel.status -ne 'passed' -or $kernel.folder -ne $folder) { throw ('Shape regression failed: ' + $kernelFile) }
    Copy-Item -LiteralPath $kernelFile -Destination (Join-Path $runRoot 'shape-kernel.json')
    @{stage07_passed=$false;transaction_regression=$true;shape_kernel_regression=$true;scope='Editor NullRHI transaction regression only';report=$reportFile} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
    Write-Output $runRoot
}
finally {
    $env:VAM_STAGE07_PROBE_CONFIG = $priorConfig
    $env:VAM_STAGE07_PROBE_REPORT = $priorReport
    $env:VAM_NATIVE_REPORT_FILE = $priorNative
}
