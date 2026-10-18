[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
& (Join-Path $PSScriptRoot 'BootstrapAssets.ps1') -EngineRoot $engine -Mode Validate
$output = New-LDArtifactDirectory 'PackageWin64'
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
# New per-run output directories avoid any recursive cleanup of an existing build.
$archive = Join-Path $output 'Archive'
$maps = '/Game/Lockdown/Generated/Maps/L_Frontend+/Game/Lockdown/Generated/Maps/L_MigrationSandbox+/Game/Lockdown/Generated/Maps/L_FunctionalTests'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/RunUAT.bat') -ToolArguments @('BuildCookRun', "-project=$project", '-noP4', '-platform=Win64', '-clientconfig=Development', '-build', '-cook', '-stage', '-pak', '-archive', "-archivedirectory=$archive", "-map=$maps", '-unattended', '-utf8output') -LogPath (Join-Path $output 'Package.log')
$executables = @(Get-ChildItem -LiteralPath $archive -Recurse -Filter 'Lockdown.exe' -File)
if ($executables.Count -eq 0) { throw "UAT returned success but no Lockdown.exe was archived: $archive" }
Write-Host "Development package archived at $archive. Runtime acceptance still requires launching it."
