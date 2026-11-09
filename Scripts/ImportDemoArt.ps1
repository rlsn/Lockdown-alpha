[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Import','ImportUI','Validate')][string]$Mode='Validate')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
# The character measurement concerns only the CC0 bodies, which ImportUI leaves as they are.
if ($Mode -ne 'ImportUI') {
    & python -B (Join-Path $PSScriptRoot 'MeasureDemoArt.py') --check | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SourceData/Art/art_assets.json disagrees with the measured character sources (Scripts/MeasureDemoArt.py --check).' }
}
$output = New-LDArtifactDirectory "DemoArt-$Mode"
$arguments = @((Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), '-run=LDDemoArtImport', '-unattended', '-nop4', '-nosound', '-nullrhi')
if ($Mode -eq 'Validate') { $arguments += '-Validate' }
if ($Mode -eq 'ImportUI') { $arguments += '-UI' }
$logPath = Join-Path $output 'DemoArt.log'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath $logPath
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'LD_DEMO_ART_SUCCESS' -Quiet)) { throw "Demo art $Mode did not confirm success. Log: $logPath" }
Write-Host "Demo art $Mode passed. Evidence: $output"
