[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,900)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B90Closure'
$legacy=Join-Path $output 'legacy'
New-Item -ItemType Directory -Path $legacy | Out-Null
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?Profile=v09_full -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -UseFixedTimeStep -FPS=60 -ExecCmds="t.MaxFPS 0" -LDB90ClosureProbe -LDWorldProbe -LDWorldBoundaryProbe -LDWorldRoute=Boundary -LDWorldCase=Sources -B90Output="{1}" -LDProbeReport="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$output,(Join-Path $legacy 'Report.json'),(Join-Path $output 'Native.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B90 reset/profile switch timed out: $output"}
    if($process.ExitCode -ne 0){throw "B90 reset/profile switch failed ($($process.ExitCode)): $output"}
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
& python -B (Join-Path $PSScriptRoot 'CheckB90Closure.py') $output
if($LASTEXITCODE -ne 0){throw "B90 reset/profile evidence failed: $output"}
& python -B (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $legacy 'Ledger.json')
if($LASTEXITCODE -ne 0){throw "Switched v08 independent ledger replay failed: $output"}
Write-Host "B90 closure evidence: $output"
