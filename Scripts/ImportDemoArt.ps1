[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Import','ImportUI','Validate')][string]$Mode='Validate')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editor = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
# The character measurement concerns only the glTF bodies, which ImportUI leaves as they are; the importer measures
# the authored bodies itself. The jersey mask is the hooper body texture's committed tint mask.
if ($Mode -ne 'ImportUI') {
    & python -B (Join-Path $PSScriptRoot 'MeasureDemoArt.py') --check | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SourceData/Art/art_assets.json disagrees with the measured character sources (Scripts/MeasureDemoArt.py --check).' }
    & python -B (Join-Path $PSScriptRoot 'MakeHooperJerseyMask.py') --check
    if ($LASTEXITCODE -ne 0) { throw 'The hooper jersey mask differs from its rebuild (Scripts/MakeHooperJerseyMask.py).' }
}
$output = New-LDArtifactDirectory "DemoArt-$Mode"
$arguments = @($project, '-run=LDDemoArtImport', '-unattended', '-nop4', '-nosound', '-nullrhi')
if ($Mode -eq 'Validate') { $arguments += '-Validate' }
if ($Mode -eq 'ImportUI') { $arguments += '-UI' }
$logPath = Join-Path $output 'DemoArt.log'
Invoke-LDTool -Tool $editor -ToolArguments $arguments -LogPath $logPath
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'LD_DEMO_ART_SUCCESS' -Quiet)) { throw "Demo art $Mode did not confirm success. Log: $logPath" }
Write-Host "Demo art $Mode passed. Evidence: $output"
