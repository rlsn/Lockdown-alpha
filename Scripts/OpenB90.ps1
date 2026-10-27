[CmdletBinding()]
param([string]$EngineRoot,[switch]$CheckWalk,[switch]$Capture,[ValidateSet('Heart','BridgeEntry')][string]$WalkCase='Heart')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
if($CheckWalk -and $Capture){throw 'Choose walk validation or rendered capture separately.'}
$output = New-LDArtifactDirectory 'B90Walk'
$arguments = '"{0}" /Game/Lockdown/Generated/V09/L_V09Full?Profile=v09_full -game -windowed -ResX=1280 -ResY=720 -nosound -DisablePlugins=AllToolsets,ModelContextProtocol -abslog="{1}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),(Join-Path $output 'Walk.log')
if($CheckWalk){$arguments+=' -unattended -nullrhi -LDB90Probe -B90Output="'+$output+'" -UseFixedTimeStep -FPS=60'}
if($CheckWalk -and $WalkCase -eq 'BridgeEntry'){$arguments+=' -LDB90BridgeEntry'}
if($Capture){$arguments+=' -unattended -RenderOffScreen -LDB90Capture -B90Output="'+$output+'" -ExecCmds="t.MaxFPS 30"'}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle $(if($CheckWalk -or $Capture){'Hidden'}else{'Normal'}) -PassThru
if($Capture){
    if(-not $process.WaitForExit(120000)){$process.Kill();throw "B90 capture timed out: $output"}
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $output 'Capital.png'))){throw "B90 capture failed: $output"}
    if(Select-String -LiteralPath (Join-Path $output 'Walk.log') -SimpleMatch 'missing usage flag InstancedStaticMeshes' -Quiet){throw "B90 capture used fallback materials: $output"}
}
if($CheckWalk){
    if(-not $process.WaitForExit(60000)){$process.Kill();throw "B90 walk timed out: $output"}
    $result=Join-Path $output 'Walk.json'
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $result)){throw "B90 walk failed: $output"}
    if(-not (Get-Content -LiteralPath $result -Raw | ConvertFrom-Json).passed){throw "B90 walk acceptance failed: $output"}
}
Write-Host "B90 spatial preview evidence: $output"
