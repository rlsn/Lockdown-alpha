[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[ValidateRange(60,1800)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
& python -B (Join-Path $PSScriptRoot 'SyncB91Data.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 contract is stale'}
$output=New-LDArtifactDirectory $(if($Capture){'B91Environment-Capture'}else{'B91Environment'})
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -game -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -UseFixedTimeStep -FPS=60 -ExecCmds="t.MaxFPS 0" -LDB91Probe -B91Output="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$output,(Join-Path $output 'Native.log')
if($Capture){$arguments+=' -RenderOffScreen -windowed -ForceRes -ResX=1600 -ResY=900 -LDB91Capture'}else{$arguments+=' -nullrhi'}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B91 fixture timed out: $output"}
    if($process.ExitCode -ne 0){throw "B91 fixture failed ($($process.ExitCode)): $output"}
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
& python -B (Join-Path $PSScriptRoot 'CheckB91Evidence.py') $output
if($LASTEXITCODE -ne 0){throw "B91 evidence rejected: $output"}
Write-Host "B91 evidence: $output"
