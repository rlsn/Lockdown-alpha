[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet(4,12)][int]$Humans=4,[int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB92Population.py') --check
if($LASTEXITCODE -ne 0){throw 'B92 population inputs are stale'}
& python -B (Join-Path $PSScriptRoot 'SyncB92Economy.py') --check
if($LASTEXITCODE -ne 0){throw 'B92 economy input is stale'}
$output=New-LDArtifactDirectory "B92Roster-$Humans"
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB92GameMode?Profile=v09_population?Humans={1} -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB92RosterProbe -B92Output="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$Humans,$output,(Join-Path $output 'Roster.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try{
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B92 roster timed out: $output"}
    if($process.ExitCode -ne 0){throw "B92 roster failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB92Roster.py') $output
    if($LASTEXITCODE -ne 0){throw "B92 roster evidence rejected: $output"}
    Write-Host "PASS B92 roster: $output"
}finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
