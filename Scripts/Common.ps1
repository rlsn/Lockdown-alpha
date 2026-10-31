Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-LDProjectRoot {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-LDProjectVersion {
    $inProjectSettings = $false
    $versions = @()
    foreach ($line in Get-Content -LiteralPath (Join-Path (Get-LDProjectRoot) 'Config/DefaultGame.ini') -Encoding utf8) {
        $value = $line.Trim()
        if ($value -match '^\[(.+)\]$') {
            $inProjectSettings = $Matches[1] -eq '/Script/EngineSettings.GeneralProjectSettings'
        } elseif ($inProjectSettings -and $value -match '^ProjectVersion\s*=\s*(.*)$') {
            $version = $Matches[1].Trim()
            if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'ProjectVersion must use major.minor.patch.' }
            $versions += $version
        }
    }
    if ($versions.Count -ne 1) { throw 'Expected exactly one ProjectVersion in GeneralProjectSettings.' }
    $versions[0]
}

function Resolve-LDEngine {
    param([string]$EngineRoot)
    $projectRoot = Get-LDProjectRoot
    if (-not $EngineRoot) { $EngineRoot = $env:LD_UE_ROOT }
    if (-not $EngineRoot) {
        $settingsPath = Join-Path $projectRoot 'Config/Local/Environment.json'
        if (Test-Path -LiteralPath $settingsPath) {
            $EngineRoot = (Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json).engineRoot
        }
    }
    if (-not $EngineRoot) {
        $association = (Get-Content -LiteralPath (Join-Path $projectRoot 'Lockdown.uproject') -Raw | ConvertFrom-Json).EngineAssociation
        $buildsKey = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
        if (Test-Path -LiteralPath $buildsKey) {
            $associationProperty = (Get-ItemProperty -LiteralPath $buildsKey).PSObject.Properties[$association]
            if ($associationProperty) { $EngineRoot = $associationProperty.Value }
        }
        if (-not $EngineRoot) {
            $installedKey = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association"
            if (Test-Path -LiteralPath $installedKey) { $EngineRoot = (Get-ItemProperty -LiteralPath $installedKey).InstalledDirectory }
        }
    }
    if (-not $EngineRoot) { throw 'Engine not found. Set LD_UE_ROOT or Config/Local/Environment.json (engineRoot).' }
    $resolvedRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    $version = Get-Content -LiteralPath (Join-Path $resolvedRoot 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
    $lock = Get-Content -LiteralPath (Join-Path $projectRoot 'Config/PlatformLock.json') -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne $lock.engine.major -or $version.MinorVersion -ne $lock.engine.minor -or $version.PatchVersion -ne $lock.engine.patch -or $version.Changelist -ne $lock.engine.changelist) {
        throw "Engine does not match pinned $($lock.engine.major).$($lock.engine.minor).$($lock.engine.patch) CL $($lock.engine.changelist)."
    }
    $resolvedRoot
}

function New-LDArtifactDirectory {
    param([Parameter(Mandatory)][string]$Name)
    $outputPath = Join-Path (Get-LDProjectRoot) ('Artifacts/{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $Name)
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $outputPath
}

function Invoke-LDTool {
    param([Parameter(Mandatory)][string]$Tool, [string[]]$ToolArguments, [Parameter(Mandatory)][string]$LogPath)
    & $Tool @ToolArguments 2>&1 | Tee-Object -FilePath $LogPath | Out-Host
    $toolExitCode = $LASTEXITCODE
    if ($toolExitCode -ne 0) { throw "Tool failed with exit code $toolExitCode. Log: $LogPath" }
}

function Assert-LDEditorModules {
    $projectRoot = Get-LDProjectRoot
    foreach ($module in @('Lockdown', 'LockdownEditor')) {
        if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "Binaries/Win64/UnrealEditor-$module.dll"))) {
            throw "Missing $module editor module. Run Preflight.ps1 and BuildEditor.ps1 successfully before launching commandlets or reopening this C++ project."
        }
    }
}

function Resolve-LDPackage {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $root = (Resolve-Path -LiteralPath $PackageRoot).Path
    # Accept the archive's Windows folder or its parent, never a workspace executable.
    if (Test-Path -LiteralPath (Join-Path $root 'Windows/Lockdown/Binaries/Win64/Lockdown.exe')) { $root = Join-Path $root 'Windows' }
    $binary = Join-Path $root 'Lockdown/Binaries/Win64/Lockdown.exe'
    if (-not (Test-Path -LiteralPath $binary)) { throw "No packaged runtime executable: $binary" }
    $paks = @(Get-ChildItem -LiteralPath (Join-Path $root 'Lockdown/Content/Paks') -File -Filter '*.pak')
    if (-not $paks.Count) { throw "No cooked pak found in $root" }
    [pscustomobject]@{ Root=$root; Binary=$binary }
}

# Read-only display metadata. An offscreen render does not exercise OS-scale window placement.
function Get-LDDisplayEnvironment {
    if(-not ('LDDisplayMetadata' -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class LDDisplayMetadata {
    [StructLayout(LayoutKind.Sequential)] public struct Point {public int X; public int Y;}
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(Point point, uint flags);
    [DllImport("shcore.dll")] public static extern int GetScaleFactorForMonitor(IntPtr monitor, out int scale);
}
"@
    }
    $monitor=[LDDisplayMetadata]::MonitorFromPoint([LDDisplayMetadata+Point]::new(),1)
    [int]$scale=0
    $result=[LDDisplayMetadata]::GetScaleFactorForMonitor($monitor,[ref]$scale)
    @{primaryMonitorScalePercent=$(if($result -eq 0){$scale}else{$null});scaleQueryResult=$result;method='Read-only primary monitor scale query; offscreen UI does not certify OS scaling/window placement'}
}
