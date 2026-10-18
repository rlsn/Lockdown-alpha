[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(30,600)][int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'MovementCheck'
$reportPath=Join-Path $output 'Movement.json'
$editorArgs='"{0}" /Game/Lockdown/Generated/Maps/L_MigrationSandbox -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -fps=60 -LDMovementProbe -LDProbeReport="{1}" -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$reportPath,(Join-Path $output 'Movement.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds*1000)) { $process.Kill(); throw "Movement check timed out: $output" }
if ($process.ExitCode -ne 0) { throw "Movement check failed with exit $($process.ExitCode): $output" }
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing movement report: $output" }
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($report.failures -ne 0 -or @($report.checks).Count -lt 7) { throw "Movement scenario incomplete/failed: $reportPath" }
$report.checks | Write-Host
Write-Host "Movement check passed. Evidence: $output"
