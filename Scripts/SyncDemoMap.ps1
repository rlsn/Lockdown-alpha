[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(0,16)][int]$MaxParallelActions=0)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
$projectRoot = Get-LDProjectRoot
$project = Join-Path $projectRoot 'Lockdown.uproject'
$editors = @(Get-CimInstance Win32_Process -Filter "name='UnrealEditor.exe'" | Where-Object {
    $_.CommandLine -and $_.CommandLine.Replace('\','/').IndexOf($project.Replace('\','/'), [StringComparison]::OrdinalIgnoreCase) -ge 0
})
if ($editors.Count) { throw 'Save and close this project editor before synchronizing its saved map and native geometry contract.' }
Assert-LDEditorModules
Set-LDPlatformValidation Validate
$output = New-LDArtifactDirectory 'SyncDemoMap'
$exportPath = Join-Path $output 'SavedScene.json'
$arguments = @($project, '-run=LDDemoArtImport', "-ExportStaticScene=$exportPath", '-unattended', '-nop4', '-nosound', '-nullrhi')
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath (Join-Path $output 'Export.log')
& python -B (Join-Path $PSScriptRoot 'ApplyDemoMapEdits.py') $exportPath
if ($LASTEXITCODE -ne 0) { throw "Saved scene export rejected. Source inputs were not committed. Evidence: $output" }
& python -B (Join-Path $PSScriptRoot 'SyncDemoWorldData.py')
if ($LASTEXITCODE -ne 0) { throw "Derived world generation failed. Evidence: $output" }
& python -B (Join-Path $PSScriptRoot 'SyncDemoContentData.py')
if ($LASTEXITCODE -ne 0) { throw "Derived content generation failed. Evidence: $output" }
& (Join-Path $PSScriptRoot 'BuildEditor.ps1') -EngineRoot $engine -MaxParallelActions $MaxParallelActions
& (Join-Path $PSScriptRoot 'BootstrapAssets.ps1') -EngineRoot $engine -Mode Regenerate
Write-Host "Saved Demo map synchronized; appearance and authored objects retained. Evidence: $output"
