[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[ValidateRange(60,900)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory $(if($Capture){'B90RiverCapture'}else{'B90River'})
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?Profile=v09_full -game -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -UseFixedTimeStep -FPS=60 -ExecCmds="t.MaxFPS 0" -LDB90RiverProbe -B90Output="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$output,(Join-Path $output 'Native.log')
if($Capture){$arguments+=' -RenderOffScreen -windowed -ResX=1280 -ResY=720 -LDB90RiverCapture'}else{$arguments+=' -nullrhi'}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B90 river fixture timed out: $output"}
    if($process.ExitCode -ne 0){throw "B90 river fixture failed ($($process.ExitCode)): $output"}
}
finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
& python -B (Join-Path $PSScriptRoot 'CheckB90River.py') $output
if($LASTEXITCODE -ne 0){throw "Independent B90 river validation failed: $output"}
Write-Host "B90 river evidence: $output"
