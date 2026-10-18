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
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/RunUAT.bat') -ToolArguments @('BuildCookRun', "-project=$project", '-noP4', '-platform=Win64', '-clientconfig=Development', '-build', '-cook', '-stage', '-pak', '-iostore', '-archive', "-archivedirectory=$archive", "-map=$maps", '-unattended', '-utf8output') -LogPath (Join-Path $output 'Package.log')
$executables = @(Get-ChildItem -LiteralPath $archive -Recurse -Filter 'Lockdown.exe' -File)
if ($executables.Count -eq 0) { throw "UAT returned success but no Lockdown.exe was archived: $archive" }
$package=Resolve-LDPackage $archive
$root=Get-LDProjectRoot
$payload=@(Get-ChildItem -LiteralPath $package.Root -Recurse -File | Where-Object {$_.Extension -in '.exe','.pak','.utoc','.ucas' -or $_.Name -eq 'AssetRegistry.bin'} | ForEach-Object {[pscustomobject]@{path=[IO.Path]::GetRelativePath($package.Root,$_.FullName);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$sources=@(Get-ChildItem -LiteralPath (Join-Path $root 'Source'),(Join-Path $root 'Config') -Recurse -File | Where-Object {$_.FullName -notlike '*\Config\Local\*'} | ForEach-Object {[pscustomobject]@{path=[IO.Path]::GetRelativePath($root,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
@{configuration='Development';platform='Win64';engine=(Get-Content (Join-Path $root 'Config/PlatformLock.json') -Raw|ConvertFrom-Json).engine;sourceRevision=(& git -C $root rev-parse HEAD);sourceDirty=[bool](& git -C $root status --porcelain);sourceFiles=$sources;files=$payload;maps=$maps.Split('+');runtime='Lockdown/Binaries/Win64/Lockdown.exe';topologyNote='Separate headless packaged listen authority with a nonparticipating local connection; two remote clients. Dedicated Server target remains conditional and unverified.'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'PackageManifest.json')
Write-Host "Development package archived at $archive. Runtime acceptance still requires launching it."
