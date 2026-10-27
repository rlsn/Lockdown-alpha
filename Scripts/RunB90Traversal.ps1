[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('All','Routes','Bounds')][string]$Lane='All',
      [ValidatePattern('^[a-z0-9_]*$')][string]$Trip='',
      [ValidateRange(60,3600)][int]$TimeoutSeconds=900)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
if($Trip -and $Lane -eq 'Bounds'){throw 'A trip filter requires a Routes or All lane.'}
$output=New-LDArtifactDirectory "B90Traversal-$Lane"
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?Profile=v09_full -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -UseFixedTimeStep -FPS=60 -ExecCmds="t.MaxFPS 0" -LDB90Traversal -B90TraversalLane={1} -B90Output="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$Lane,$output,(Join-Path $output 'Native.log')
if($Trip){$arguments+=' -B90Trip='+$Trip}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B90 traversal timed out: $output"}
    if($process.ExitCode -ne 0){throw "B90 traversal failed ($($process.ExitCode)): $output"}
    $report=Get-Content -LiteralPath (Join-Path $output 'Traversal.json') -Raw | ConvertFrom-Json
    if(-not $report.complete -or $report.failures){throw "Incomplete/failed B90 traversal: $output"}
}
finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
& python -B (Join-Path $PSScriptRoot 'CheckB90Traversal.py') $output
if($LASTEXITCODE -ne 0){throw "Independent B90 trace validation failed: $output"}
Write-Host "B90 traversal evidence: $output"
