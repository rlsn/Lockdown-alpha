Set-StrictMode -Version Latest

function Get-LDDemoMaps {
    @('/Game/Lockdown/Generated/Maps/L_Frontend', '/Game/Lockdown/Generated/Maps/L_Demo')
}

function Assert-LDDemoInputs {
    $compiler = Join-Path $PSScriptRoot 'SyncDemoContentData.py'
    if (-not (Test-Path -LiteralPath $compiler)) {
        throw 'Missing Demo content compiler. Legacy catalogs cannot be used for the Demo.'
    }
    & python -B $compiler --check
    if ($LASTEXITCODE -ne 0) {
        throw 'Demo inputs are unavailable or inconsistent. Approved content, the world payload and generated native contracts are required; no legacy fallback is allowed.'
    }
}

function Assert-LDDemoPackage {
    param([Parameter(Mandatory)]$Package)
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $Package.Root '..\..\PackageManifest.json'))
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw 'Missing Demo package manifest. This launcher does not run legacy or unverified packages.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $version = $manifest.PSObject.Properties['demoEntryContractVersion']
    if (-not $version -or $version.Value -ne 1) { throw 'Retired package entry contract. Build and validate the single Demo package before launching it.' }
    $expected = @(Get-LDDemoMaps)
    if (@(Compare-Object -ReferenceObject $expected -DifferenceObject @($manifest.maps)).Count -ne 0) {
        throw 'Package map set does not match the Demo contract.'
    }
    $runtime = @($manifest.files | Where-Object { $_.path.Replace('/', '\') -eq 'Lockdown\Binaries\Win64\Lockdown.exe' })
    if ($runtime.Count -ne 1 -or (Get-FileHash -LiteralPath $Package.Binary -Algorithm SHA256).Hash -ne $runtime[0].sha256) {
        throw 'Packaged executable does not match its Demo manifest.'
    }
}

# One launch description for owned-connection runners: the editor game target, or an archived Development
# package whose manifest matches its executable. Probes accept evidence only below their own project's
# Artifacts, which for a package lies inside the package; Complete-LDGameEvidence moves it back out.
function Get-LDGameLaunch {
    param([string]$EngineRoot, [string]$PackageRoot, [Parameter(Mandatory)][string]$Output)
    if ($PackageRoot) {
        $package = Resolve-LDPackage $PackageRoot
        Assert-LDDemoPackage $package
        $probeRoot = Join-Path $package.Root 'Lockdown\Artifacts'
        $probeOutput = Join-Path $probeRoot (Split-Path $Output -Leaf)
        New-Item -ItemType Directory -Path $probeOutput | Out-Null
        return [pscustomobject]@{ Packaged = $true; Binary = $package.Binary; Project = ''; Package = $package.Root
            Output = $Output; ProbeRoot = $probeRoot; ProbeOutput = $probeOutput }
    }
    $engine = Resolve-LDEngine $EngineRoot
    Assert-LDEditorModules
    [pscustomobject]@{ Packaged = $false; Binary = (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')
        Project = (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'); Package = ''; Output = $Output; ProbeRoot = ''; ProbeOutput = $Output }
}

# Editor-only switches stay out of packaged command lines; an empty URL opens the default frontend map.
function Format-LDGameArguments {
    param([Parameter(Mandatory)]$Launch, [string]$Url, [Parameter(Mandatory)][string]$Arguments)
    $target = if ($Url) { '"{0}" ' -f $Url } else { '' }
    if ($Launch.Packaged) { return $target + $Arguments }
    '"{0}" {1}-game -DisablePlugins=AllToolsets,ModelContextProtocol,Fab {2}' -f $Launch.Project, $target, $Arguments
}

# Moves packaged probe evidence next to the runner logs and leaves the package without test output.
function Complete-LDGameEvidence {
    param([Parameter(Mandatory)]$Launch)
    if (-not $Launch.Packaged -or -not (Test-Path -LiteralPath $Launch.ProbeOutput)) { return }
    Get-ChildItem -LiteralPath $Launch.ProbeOutput -Force | Move-Item -Destination $Launch.Output -Force
    Remove-Item -LiteralPath $Launch.ProbeOutput
    if (-not @(Get-ChildItem -LiteralPath $Launch.ProbeRoot -Force).Count) { Remove-Item -LiteralPath $Launch.ProbeRoot }
}
