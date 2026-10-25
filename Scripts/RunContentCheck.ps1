[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B75-Content'
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$arguments='"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=v07_integration -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDContentProbe -LDProbeReport="{1}" -abslog="{2}"' -f $project,(Join-Path $output 'Effects.json'),(Join-Path $output 'Effects.log')
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B75 effect fixtures timed out: $output"};if($process.ExitCode -ne 0){throw "B75 effect fixtures failed ($($process.ExitCode)): $output"}}
finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
$report=Get-Content -LiteralPath (Join-Path $output 'Effects.json') -Raw|ConvertFrom-Json
if($report.failures -or @($report.checks).Count -lt 200){throw "Incomplete B75 effect evidence: $output"}
& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output 'Ledger.json');if($LASTEXITCODE -ne 0){throw 'Independent B75 replay failed'}
Write-Host "B75 native authority passed $(@($report.checks).Count) checks. Evidence: $output"
