param(
 [string]$GameRoot='F:\Virt A Mate',
 [string]$OutputRoot=(Join-Path $PSScriptRoot '..\Saved\HairRestoration'),
 [string]$HairPresetPath="",
 [int]$TimeoutSeconds=900,
 [switch]$Dynamic
)
# Diagnostic instrumentation only. Original scene/preset files are never changed.
# Run with VaM closed. The harness currently targets this project's audited appearance.
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameRoot).Path
$out=(Resolve-Path -LiteralPath $OutputRoot).Path
$runId=[Guid]::NewGuid().ToString('N')
$plugins=[IO.Path]::GetFullPath((Join-Path $game 'BepInEx\plugins'))
$backup=[IO.Path]::GetFullPath((Join-Path $game ('BepInEx\plugins.C1-backup-'+$runId)))
foreach($path in @($plugins,$backup)) {
 if(-not $path.StartsWith($game+'\BepInEx\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected plugin path'}
}
if(Get-Process VaM -ErrorAction SilentlyContinue){throw 'VaM is already running'}
$dll=Join-Path $out 'C1Capture.dll'
if($game -ne 'F:\Virt A Mate' -or -not $out.StartsWith('I:\Document\UE5\SmartNPC\Plugins\VamResourceBrowser\Saved\HairRestoration\',[StringComparison]::OrdinalIgnoreCase)) {
 throw 'Hair restoration requires its own output; historical evidence is protected'
}
if(Test-Path -LiteralPath (Join-Path $out 'runtime')){throw 'Archive the previous runtime evidence before launching another capture'}
$references=Get-ChildItem -LiteralPath (Join-Path $game 'VaM_Data\Managed') -Filter 'UnityEngine*.dll' | ForEach-Object { '/r:'+$_.FullName }
& 'C:\Windows\Microsoft.NET\Framework64\v3.5\csc.exe' /nologo /target:library ('/out:'+$dll) ('/r:'+(Join-Path $game 'BepInEx\core\BepInEx.dll')) ('/r:'+(Join-Path $game 'VaM_Data\Managed\Assembly-CSharp.dll')) @references (Join-Path $PSScriptRoot 'VamC1Capture.cs') (Join-Path $PSScriptRoot 'VamHairRestCapture.cs') (Join-Path $PSScriptRoot 'VamHairDynamicCapture.cs')
if($LASTEXITCODE -ne 0){throw 'Diagnostic sampler compilation failed'}
$inventory=Get-ChildItem -LiteralPath $plugins -File -Recurse | ForEach-Object {
 [PSCustomObject]@{Path=$_.FullName;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}
$inventory | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $out 'original-plugins.json') -Encoding utf8
$backup | Set-Content (Join-Path $out 'recovery-plugin-path.txt')
Move-Item -LiteralPath $plugins -Destination $backup
$process=$null
try {
 New-Item -ItemType Directory -Path $plugins | Out-Null
 Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins 'C1Capture.dll')
 if($HairPresetPath){$env:VAM_HAIR_PRESET=(Resolve-Path -LiteralPath $HairPresetPath).Path}
 $env:VAM_HAIR_REST_AUDIT='1'
 if($Dynamic){$env:VAM_HAIR_DYNAMIC_AUDIT='1'}
 $env:VAM_AUDIT_OUTPUT=Join-Path $out 'runtime'
 $process=Start-Process -FilePath (Join-Path $game 'VaM.exe') -ArgumentList '-vrmode None -screen-fullscreen 0 -screen-width 960 -screen-height 720' -WorkingDirectory $game -WindowStyle Hidden -PassThru
 $process.Id | Set-Content (Join-Path $out 'owned-vam.pid')
 if(-not $process.WaitForExit($TimeoutSeconds*1000)) {
  Stop-Process -Id $process.Id
  'Capture timed out; partial data is not completion' | Set-Content (Join-Path $out 'launcher-timeout.txt')
 }
} finally {
 Remove-Item Env:VAM_HAIR_PRESET -ErrorAction SilentlyContinue
 Remove-Item Env:VAM_HAIR_REST_AUDIT -ErrorAction SilentlyContinue
 Remove-Item Env:VAM_HAIR_DYNAMIC_AUDIT -ErrorAction SilentlyContinue
 Remove-Item Env:VAM_AUDIT_OUTPUT -ErrorAction SilentlyContinue
 if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
 if(Test-Path -LiteralPath $plugins){Move-Item -LiteralPath $plugins -Destination (Join-Path $out ('instrumentation-'+$runId))}
 Move-Item -LiteralPath $backup -Destination $plugins
 foreach($item in $inventory){if((Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.SHA256){throw ('Restored plugin hash mismatch: '+$item.Path)}}
 'Original plugins restored and hashes verified' | Set-Content (Join-Path $out 'restored.txt')
}

if(-not (Test-Path -LiteralPath (Join-Path $out "runtime\complete.txt"))){throw "VaM capture did not complete; see preserved runtime and launcher logs"}
