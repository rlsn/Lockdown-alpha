[CmdletBinding()]
param([string]$EngineRoot,[int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB91Forest.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 fitted forest contract is stale'}
$output=New-LDArtifactDirectory 'B91Forest-Routes'
$argsForRoutes='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB91ForestRoutes -B91Output="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$output,(Join-Path $output 'ForestRoutes.log')
$p=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $argsForRoutes -WindowStyle Hidden -PassThru
try{if(-not $p.WaitForExit($TimeoutSeconds*1000)){throw "Forest routes timed out: $output"};if($p.ExitCode -ne 0){throw "Forest routes failed: $output"};$r=Get-Content (Join-Path $output 'ForestRoutes.json') -Raw | ConvertFrom-Json;if(-not $r.complete -or $r.failures -or $r.routes.Count -ne 864 -or @($r.routes | Where-Object {-not $_.passed}).Count){throw "Forest route evidence rejected: $output"};Write-Host "PASS864 forest routes: $output"}finally{if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}
