[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(30,300)][int]$TimeoutSeconds=90,[switch]$VariableFrames)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory $(if($VariableFrames){'CombatCheck-VariableFrames'}else{'CombatCheck'})
$reportPath=Join-Path $output 'Combat.json'
$editorArgs='"{0}" /Game/Lockdown/Generated/Maps/L_MigrationSandbox -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -fps=60 -LDCombatProbe -LDProbeReport="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$reportPath,(Join-Path $output 'Combat.log')
if($VariableFrames){$editorArgs=$editorArgs.Replace('-benchmark -fps=60','-ExecCmds="t.MaxFPS 62"')}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit($TimeoutSeconds*1000)){$process.Kill();throw "Combat probe timed out: $output"}
if($process.ExitCode -ne 0){throw "Combat probe failed with exit $($process.ExitCode): $output"}
if(-not(Test-Path -LiteralPath $reportPath)){throw "Missing combat report: $output"}
$report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
if($report.failures -ne 0 -or @($report.checks).Count -lt 48){throw "Combat evidence failed or incomplete: $reportPath"}
$report.checks|Write-Host
Write-Host "Combat probe passed. Evidence: $output"
