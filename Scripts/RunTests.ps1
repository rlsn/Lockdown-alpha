[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Lockdown.Rules')][string]$Suite='Lockdown.Rules', [ValidateRange(30,1800)][int]$TimeoutSeconds=900)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'Automation'
$reportPath = Join-Path $output 'Report'
$fixturePath = Join-Path $output 'DemoContentFixtures'
& python -B (Join-Path $PSScriptRoot 'ExportDemoNativeFixtures.py') --output $fixturePath
if ($LASTEXITCODE -ne 0) { throw "Native Demo content fixtures failed to export: $fixturePath" }
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editorArgs = '"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests {1}" -TestExit="Automation Test Queue Empty" -ReportExportPath="{2}" -abslog="{3}" -LDDemoContentFixtures="{4}"' -f $project,$Suite,$reportPath,(Join-Path $output 'Automation.log'),$fixturePath
$process = Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    throw "Automation timed out after $TimeoutSeconds seconds. Evidence: $output"
}
if ($process.ExitCode -ne 0) { throw "Automation process failed: $($process.ExitCode). Evidence: $output" }
$indexPath = Join-Path $reportPath 'index.json'
if (-not (Test-Path -LiteralPath $indexPath)) { throw "No automation report generated: $output" }
& python -B (Join-Path $PSScriptRoot 'CheckRuleReport.py') $indexPath
if ($LASTEXITCODE -ne 0) { throw "Automation report failed the required rule-test set: $indexPath" }
Write-Host "Automation passed. Evidence: $output"
