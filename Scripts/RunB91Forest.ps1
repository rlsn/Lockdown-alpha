[CmdletBinding()]
param([string]$EngineRoot,[switch]$Fit,[int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB91Forest.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 fitted forest contract is stale'}
$output=New-LDArtifactDirectory $(if($Fit){'B91Forest-Fit'}else{'B91Forest-Probe'})
$flag=if($Fit){'LDB91ForestFit'}else{'LDB91ForestProbe'}
$forestArguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -{1}="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$flag,(Join-Path $output 'Forest.json'),(Join-Path $output 'Forest.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $forestArguments -WindowStyle Hidden -PassThru
try{
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Forest timed out: $output"}
    if($process.ExitCode -ne 0){throw "Forest failed: $output"}
    $r=Get-Content (Join-Path $output 'Forest.json') -Raw | ConvertFrom-Json
    if($r.rows.Count -ne 3000){throw "Forest census failed: $output"}
    if(-not $Fit -and ($r.walking_blockers -ne 3000 -or $r.sight_blockers -ne 3000 -or -not $r.not_replicated -or -not $r.no_tick -or -not $r.navigation)){throw "Forest proxy checks failed: $output"}
    Write-Host "PASS B91 forest: $output"
}finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
