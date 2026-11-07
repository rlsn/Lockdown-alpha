# Runs the Lockdown.Rules automation tests in a headless editor and checks the report against CheckRuleReport.py.
#   .\Scripts\RunTests.ps1                                  # the gate: every required test
#   .\Scripts\RunTests.ps1 -Filter Lockdown.Rules.DemoGrowth # development only: tests under these path prefixes
#   .\Scripts\RunTests.ps1 -ImportTests Auto                # leave out the data-import group unless its inputs changed
# A -Filter run is partial and never a gate. -ImportTests Skip/Auto decide only the data-import group (CheckRuleReport
# IMPORT_TESTS); Auto asks CIChangeScope.py whether this worktree changed any of its inputs since its merge base with
# -Base, the same decision the PR check makes.
[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Lockdown.Rules')][string]$Suite='Lockdown.Rules', [ValidateRange(30,1800)][int]$TimeoutSeconds=900,
    [string[]]$Filter, [ValidateSet('Run','Skip','Auto')][string]$ImportTests='Run', [string]$Base='origin/main')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
if ($ImportTests -eq 'Auto') {
    $decision = @(& python -B (Join-Path $PSScriptRoot 'CIChangeScope.py') --base $Base --worktree)
    if ($LASTEXITCODE -ne 0) { throw "CIChangeScope.py could not decide the data-import tests against $Base." }
    $line = @($decision | Where-Object { $_ -match '^import_tests=(run|skip)$' })
    if ($line.Count -ne 1) { throw "CIChangeScope.py printed no single import_tests decision: $decision" }
    $ImportTests = if ($line[0] -eq 'import_tests=run') { 'Run' } else { 'Skip' }
    Write-Host "Data-import tests: $ImportTests (changes since the merge base with $Base)."
}
$selection = @()
foreach ($prefix in @($Filter)) { if ($prefix) { $selection += @('--prefix', $prefix) } }
if ($ImportTests -eq 'Skip') { $selection += '--skip-import' }
$planText = & python -B (Join-Path $PSScriptRoot 'CheckRuleReport.py') --plan @selection | Out-String
if ($LASTEXITCODE -ne 0) { throw 'CheckRuleReport.py could not plan the selected rule tests.' }
$plan = $planText | ConvertFrom-Json
if ($plan.partial) { Write-Host "PARTIAL RUN, NOT A GATE: $($plan.count) of $($plan.required) required rule tests ($($Filter -join ', '))." }
if (@($plan.skipped_import).Count) { Write-Host "Leaving out the data-import group: $($plan.skipped_import -join ', ')." }
$output = New-LDArtifactDirectory 'Automation'
$reportPath = Join-Path $output 'Report'
# Fixtures are reused from the cache while every input they are built from is unchanged; tests only read them.
$export = @(& python -B (Join-Path $PSScriptRoot 'ExportDemoNativeFixtures.py') --cache (Join-Path (Get-LDProjectRoot) 'Artifacts/DemoContentFixtureCache'))
if ($LASTEXITCODE -ne 0) { throw "Native Demo content fixtures failed to export: $($export -join ' ')" }
$export | Where-Object { $_ -notmatch '^FIXTURES=' } | Out-Host
$fixtureLine = @($export | Where-Object { $_ -match '^FIXTURES=' })
if ($fixtureLine.Count -ne 1) { throw "The fixture export named no single fixture directory: $($export -join ' ')" }
$fixturePath = $fixtureLine[0].Substring('FIXTURES='.Length)
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editorArgs = '"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests {1}" -TestExit="Automation Test Queue Empty" -ReportExportPath="{2}" -abslog="{3}" -LDDemoContentFixtures="{4}"' -f $project,$plan.filter,$reportPath,(Join-Path $output 'Automation.log'),$fixturePath
$process = Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    throw "Automation timed out after $TimeoutSeconds seconds. Evidence: $output"
}
if ($process.ExitCode -ne 0) { throw "Automation process failed: $($process.ExitCode). Evidence: $output" }
$indexPath = Join-Path $reportPath 'index.json'
if (-not (Test-Path -LiteralPath $indexPath)) { throw "No automation report generated: $output" }
& python -B (Join-Path $PSScriptRoot 'CheckRuleReport.py') $indexPath @selection
if ($LASTEXITCODE -ne 0) { throw "Automation report failed the required rule-test set: $indexPath" }
if ($plan.partial) { Write-Host "Partial automation passed (not a gate). Evidence: $output" }
else { Write-Host "Automation passed. Evidence: $output" }
