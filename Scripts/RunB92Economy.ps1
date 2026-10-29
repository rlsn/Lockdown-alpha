[CmdletBinding()]
param([string]$EngineRoot,[int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB92Economy.py') --check
if($LASTEXITCODE -ne 0){throw 'B92 economy staging is stale'}
$output=New-LDArtifactDirectory 'B92Economy-Authority'
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -LDB92EconomyProbe -B92Output="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$output,(Join-Path $output 'Economy.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try{
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B92 economy timed out: $output"}
    if($process.ExitCode -ne 0){throw "B92 economy failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB92Economy.py') $output
    if($LASTEXITCODE -ne 0){throw "B92 authority evidence failed: $output"}
    Write-Host "PASS B92 economy authority: $output"
}finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
