[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
$output = New-LDArtifactDirectory 'DemoEntry-ScriptTests'
$checks = [Collections.Generic.List[string]]::new()

function Assert-Case {
    param([bool]$Passed, [string]$Name)
    if (-not $Passed) { throw "Demo entry check failed: $Name" }
    $checks.Add($Name)
}

foreach ($entry in @(
    @{ File = 'OpenStandalone.ps1'; Retired = @('Profile', 'NavigationCheckpoint') },
    @{ File = 'OpenPackaged.ps1'; Retired = @('Profile') },
    @{ File = 'BootstrapAssets.ps1'; Retired = @('RebuildRegionMaps', 'RebuildV08Region') },
    @{ File = 'RunDemoEntryCheck.ps1'; Retired = @('Profile') },
    @{ File = 'RunDemoFrontendCheck.ps1'; Retired = @('Profile') },
    @{ File = 'PackageWin64.ps1'; Retired = @('Profile', 'Map') }
)) {
    $path = Join-Path $PSScriptRoot $entry.File
    $tokens = $null
    $errors = $null
    $tree = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    Assert-Case ($errors.Count -eq 0) "$($entry.File) parses"
    $parameters = @($tree.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
    foreach ($name in $entry.Retired) {
        Assert-Case ($name -notin $parameters) "$($entry.File) no longer exposes $name"
        $arguments = @{ $name = 'retired' }
        if ($entry.File -eq 'OpenPackaged.ps1') { $arguments.PackageRoot = 'not-opened' }
        $rejected = $false
        try { & $path @arguments }
        catch { $rejected = $_.FullyQualifiedErrorId -like 'NamedParameterNotFound*' }
        Assert-Case $rejected "$($entry.File) rejects $name before executing"
    }
}
$maps = @(Get-LDDemoMaps)
Assert-Case ($maps.Count -eq 2 -and $maps[0] -eq '/Game/Lockdown/Generated/Maps/L_Frontend' -and $maps[1] -eq '/Game/Lockdown/Generated/Maps/L_Demo') 'Only frontend and Demo packages are selected'

$fixture = Join-Path $output 'PackageFixture'
$packageRoot = Join-Path $fixture 'Archive\Windows'
$binary = Join-Path $packageRoot 'Lockdown\Binaries\Win64\Lockdown.exe'
$manifestPath = Join-Path $fixture 'PackageManifest.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $binary) -Force | Out-Null
try {
    [IO.File]::WriteAllText($binary, 'Non-executable isolated manifest fixture')
    $package = [pscustomobject]@{ Root = $packageRoot; Binary = $binary }
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Missing Demo package manifest*' }
    Assert-Case $rejected 'Package without a Demo manifest is rejected'
    $manifest = @{
        demoEntryContractVersion = 1
        maps = $maps
        files = @(@{ path = 'Lockdown\Binaries\Win64\Lockdown.exe'; sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash })
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Assert-LDDemoPackage $package
    $checks.Add('Exact isolated manifest and binary bytes match; no executable is launched')
    $manifest.demoEntryContractVersion = 0
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Retired package entry contract*' }
    Assert-Case $rejected 'Old package contract is rejected'
    $manifest.demoEntryContractVersion = 1
    $manifest.maps = @($maps[0], '/Game/Lockdown/Generated/Maps/L_DevelopmentRegion')
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Package map set*' }
    Assert-Case $rejected 'Legacy package map is rejected'
    $manifest.maps = $maps
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    [IO.File]::AppendAllText($binary, ' changed')
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Packaged executable does not match*' }
    Assert-Case $rejected 'Stale packaged binary is rejected'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
@{ passed = $true; checks = @($checks) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'EntryChecks.json') -Encoding utf8
Write-Host "Demo entry script checks passed: $($checks.Count). Evidence: $output"
