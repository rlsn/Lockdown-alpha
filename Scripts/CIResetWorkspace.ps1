# Bounds the self-hosted runner's single shared workspace before each CI job. Runs only inside a GitHub Actions job,
# on its own checkout. Keeps Binaries/Intermediate for incremental builds unless they pass -MaxBuildGB, removes earlier
# runs' outputs (already uploaded or reported), logs, crash dumps and staged packages, and stops the job when the drive
# would be left with less than -MinFreeGB.
[CmdletBinding()]
param([ValidateRange(1, 1000)][int]$MaxBuildGB = 12, [ValidateRange(1, 1000)][int]$MinFreeGB = 10)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$root = Get-LDProjectRoot
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:GITHUB_WORKSPACE -or
    [IO.Path]::GetFullPath($env:GITHUB_WORKSPACE).TrimEnd('\') -ne [IO.Path]::GetFullPath($root).TrimEnd('\')) {
    throw "CIResetWorkspace.ps1 only runs on a CI job's own checkout; refusing to clean $root."
}

function Get-DirectoryBytes([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return [long]0 }
    [long](([IO.DirectoryInfo]$Path).EnumerateFiles('*', [IO.SearchOption]::AllDirectories) | Measure-Object -Property Length -Sum).Sum
}

function Clear-Directory([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Get-ChildItem -LiteralPath $Path -Force | Remove-Item -Recurse -Force }
}

& git -C $root clean -ffd
if ($LASTEXITCODE -ne 0) { throw 'git clean failed.' }
# Earlier runs' outputs: uploaded as artifacts or already reported in their own run. The native fixture cache stays: it
# is keyed by its inputs and every file is verified before reuse (ExportDemoNativeFixtures.py --cache).
$fixtureCache = 'DemoContentFixtureCache'
if (Test-Path -LiteralPath (Join-Path $root 'Artifacts')) {
    Get-ChildItem -LiteralPath (Join-Path $root 'Artifacts') -Force | Where-Object Name -ne $fixtureCache | Remove-Item -Recurse -Force
}
foreach ($relative in 'Saved/Logs', 'Saved/Crashes', 'Saved/Autosaves', 'Saved/StagedBuilds') {
    Clear-Directory (Join-Path $root $relative)
}
$buildPaths = @('Binaries', 'Intermediate') | ForEach-Object { Join-Path $root $_ }
$buildBytes = ($buildPaths | ForEach-Object { Get-DirectoryBytes $_ } | Measure-Object -Sum).Sum
if ($buildBytes -gt [long]$MaxBuildGB * 1GB) {
    Write-Host ('Build output is {0:N1} GB (cap {1} GB): removing Binaries and Intermediate; this run builds from scratch.' -f ($buildBytes / 1GB), $MaxBuildGB)
    foreach ($path in $buildPaths) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force } }
    $buildBytes = 0
}
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($root))
$freeGB = $drive.AvailableFreeSpace / 1GB
Write-Host ('Workspace {0:N1} GB (build output {1:N1} GB); {2:N1} GB free on {3}.' -f ((Get-DirectoryBytes $root) / 1GB), ($buildBytes / 1GB), $freeGB, $drive.Name)
if ($freeGB -lt $MinFreeGB) {
    throw ('Only {0:N1} GB free on {1} (need {2} GB for a build and package). Free space on this drive, then re-run.' -f $freeGB, $drive.Name, $MinFreeGB)
}
if (-not $env:LD_UE_ROOT) { throw 'LD_UE_ROOT is not set in the runner .env (Scripts/CIRunner.ps1 Install writes it).' }
