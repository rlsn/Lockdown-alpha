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
