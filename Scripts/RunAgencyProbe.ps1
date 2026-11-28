# C-10 prototype criteria probe (docs/systems/COMBAT_AND_GROWTH.md#隔离原型): runs Lockdown.Probe.AgencyCriteria in a
# headless editor. Each scenario runs with the prototype off, V-Cut and V-Next; the report is
# Artifacts/ProtoAgency/agency-probe-*.json and the log has one LD_PROTO_AGENCY_PROBE line per run and LD_PROTO_AGENCY_CRITERIA
# per variant. Prototype parameters pass through as the server switches:
#   .\Scripts\RunAgencyProbe.ps1
#   .\Scripts\RunAgencyProbe.ps1 -Switches '-LDProtoAgencyThreshold=2.2','-LDProtoAgencyRollFrom=4'
[CmdletBinding()]
param([string]$EngineRoot, [string[]]$Switches = @(), [ValidateRange(30,1800)][int]$TimeoutSeconds = 600)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
foreach ($switch in $Switches) { if ($switch -notmatch '^-LDProtoAgency[A-Za-z]*(=[0-9A-Za-z.]+)?$') { throw "Not an agency prototype switch: $switch" } }
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'AgencyProbe'
$export = @(& python -B (Join-Path $PSScriptRoot 'ExportDemoNativeFixtures.py') --cache (Join-Path (Get-LDProjectRoot) 'Artifacts/DemoContentFixtureCache') --profile runtime)
if ($LASTEXITCODE -ne 0) { throw "Native Demo content fixtures failed to export: $($export -join ' ')" }
$fixtureLine = @($export | Where-Object { $_ -match '^FIXTURES=' })
if ($fixtureLine.Count -ne 1) { throw "The fixture export named no single fixture directory: $($export -join ' ')" }
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$log = Join-Path $output 'Probe.log'
# The same headless editor setup as RunTests.ps1.
$editorArgs = '"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -EnablePlugins=GameFeatures -ini:Engine:[/Script/EngineSettings.GameMapsSettings]:EditorStartupMap=/Engine/Maps/Entry -ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests Lockdown.Probe.AgencyCriteria" -TestExit="Automation Test Queue Empty" -ReportExportPath="{1}" -abslog="{2}" -LDDemoContentFixtures="{3}" {4}' -f $project,(Join-Path $output 'Report'),$log,$fixtureLine[0].Substring('FIXTURES='.Length),($Switches -join ' ')
$process = Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); throw "The agency probe timed out. Evidence: $output" }
if ($process.ExitCode -ne 0) { throw "The agency probe failed: $($process.ExitCode). Evidence: $output" }
$index = Join-Path $output 'Report/index.json'
if (-not (Test-Path -LiteralPath $index)) { throw "No automation report. Evidence: $output" }
$state = @((Get-Content -LiteralPath $index -Raw -Encoding utf8 | ConvertFrom-Json).tests | Where-Object { $_.fullTestPath -eq 'Lockdown.Probe.AgencyCriteria' } | ForEach-Object { $_.state })
if ($state -ne 'Success') { throw "The agency probe did not succeed ($state). Evidence: $output" }
$lines = @(Select-String -LiteralPath $log -Pattern 'LD_PROTO_AGENCY_(PROBE|CRITERIA)' | ForEach-Object { $_.Line -replace '^.*?(LD_PROTO_AGENCY_)', '$1' })
if (-not ($lines -match '^LD_PROTO_AGENCY_PROBE_REPORT ')) { throw "The agency probe wrote no report. Evidence: $output" }
$lines | Out-Host
