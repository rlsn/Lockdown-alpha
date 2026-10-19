[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Lockdown.Rules')][string]$Suite='Lockdown.Rules', [ValidateRange(30,1800)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'Automation'
$reportPath = Join-Path $output 'Report'
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editorArgs = '"{0}" -unattended -nop4 -nosound -nullrhi -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests {1}" -TestExit="Automation Test Queue Empty" -ReportExportPath="{2}" -abslog="{3}"' -f $project,$Suite,$reportPath,(Join-Path $output 'Automation.log')
$process = Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    throw "Automation timed out after $TimeoutSeconds seconds. Evidence: $output"
}
if ($process.ExitCode -ne 0) { throw "Automation process failed: $($process.ExitCode). Evidence: $output" }
$indexPath = Join-Path $reportPath 'index.json'
if (-not (Test-Path -LiteralPath $indexPath)) { throw "No automation report generated: $output" }
$report = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
if ($report.failed -gt 0 -or $report.notRun -gt 0 -or $report.inProcess -gt 0 -or $report.succeeded -lt 12) {
    throw "Automation report has failures, incomplete tests, or fewer than the twelve expected rule tests: $indexPath"
}
Write-Host "Automation passed: $($report.succeeded) tests. Evidence: $output"
