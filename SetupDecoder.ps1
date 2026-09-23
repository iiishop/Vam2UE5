param([string]$Python = 'I:\Program\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe')
$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'Saved\Python'
& $Python -m pip install --target $target 'UnityPy==1.25.3' 'numpy==2.2.6'
if ($LASTEXITCODE -ne 0) { throw 'Decoder dependency setup failed. Use Python 3.11 x64 with pip.' }
Write-Host 'Private decoder dependencies installed. Restart the browser service.'
