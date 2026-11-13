# Runs the smallest local check for what this worktree changed since its merge base with -Base (docs/DEVELOPMENT.md).
#   .\Scripts\Verify.ps1                                    # decide from the changed files
#   .\Scripts\Verify.ps1 -Plan                              # only list what would run
#   .\Scripts\Verify.ps1 -Filter Lockdown.Rules.DemoGrowth  # also run the rule tests under these prefixes
# Docs and workflows need nothing. Python scripts run their own Test<module>.py. SourceData and generated data run both
# Sync --check and every Python test. Source and the .uproject run one incremental BuildEditor.ps1. The rule tests are
# gated by the PR check, so they run here only on request (-Filter); such a run is partial and never a gate.
[CmdletBinding()]
param([string]$EngineRoot, [string]$Base='origin/main', [string[]]$Filter, [switch]$Plan)
. (Join-Path $PSScriptRoot 'Common.ps1')
$root = Get-LDProjectRoot

function Invoke-LDGit {
    $output = @(& git -C $root @args)
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed." }
    $output
}

$mergeBase = @(Invoke-LDGit merge-base $Base HEAD)[0]
# core.quotepath=false keeps non-ASCII paths readable; the lists cover committed, uncommitted and untracked files.
$paths = @(Invoke-LDGit -c core.quotepath=false diff --name-only $mergeBase) + @(Invoke-LDGit -c core.quotepath=false ls-files --others --exclude-standard)
$paths = @($paths | Where-Object { $_ } | Sort-Object -Unique)

$source = @($paths | Where-Object { $_ -match '^Source/' -or $_ -match '^[^/]+\.uproject$' })
$data = @($paths | Where-Object { $_ -match '^SourceData/' -or $_ -match '^Content/Lockdown/Data/' })
$python = @($paths | Where-Object { $_ -match '^Scripts/[^/]+\.py$' })
$unchecked = @($paths | Where-Object {
    $_ -notmatch '^docs/' -and $_ -notmatch '\.md$' -and $_ -notmatch '^\.github/' -and $_ -notin $source -and $_ -notin $data -and $_ -notin $python })

# A changed Scripts module runs its own test file. Any module without one, or a data change, runs every Python test.
$patterns = @()
$allPython = $data.Count -gt 0
foreach ($path in $python) {
    $name = [IO.Path]::GetFileNameWithoutExtension($path)
    $test = if ($name -like 'Test*') { "$name.py" } else { "Test$name.py" }
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot $test)) { $patterns += $test } else { $allPython = $true }
}
$patterns = @($patterns | Sort-Object -Unique)

$steps = @()
if ($data.Count) { $steps += 'Sync --check (SyncDemoWorldData.py, SyncDemoContentData.py)' }
if ($allPython) { $steps += 'All Python tests (RunPythonTests.py)' }
elseif ($patterns.Count) { $steps += "Python tests: $($patterns -join ', ')" }
if ($source.Count) { $steps += 'Incremental BuildEditor.ps1' }
if ($Filter) { $steps += "Rule tests (partial, not a gate): $($Filter -join ', ')" }

Write-Host "$($paths.Count) changed files since the merge base with $Base ($($mergeBase.Substring(0, 8)))."
if (-not $steps.Count) { Write-Host 'Nothing to run locally for this change.' }
else { $steps | ForEach-Object { Write-Host "  - $_" } }
if ($unchecked.Count) {
    Write-Host "No automatic local check for $($unchecked.Count) files (config, assets, PowerShell); pick one from docs/systems/VERIFICATION.md if the change needs it:"
    $unchecked | Select-Object -First 8 | ForEach-Object { Write-Host "    $_" }
}
if (($source.Count -or $data.Count -or $unchecked.Count) -and -not $Filter) { Write-Host 'Rule tests are left to the PR check; pass -Filter <prefix> to run specific ones here.' }
if ($Plan) { return }

function Invoke-LDPython {
    & python -B @args | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "python $($args -join ' ') failed with exit code $LASTEXITCODE." }
}

if ($data.Count) {
    Invoke-LDPython (Join-Path $PSScriptRoot 'SyncDemoWorldData.py') --check
    Invoke-LDPython (Join-Path $PSScriptRoot 'SyncDemoContentData.py') --check
}
if ($allPython) { Invoke-LDPython (Join-Path $PSScriptRoot 'RunPythonTests.py') }
else { foreach ($pattern in $patterns) { Invoke-LDPython (Join-Path $PSScriptRoot 'RunPythonTests.py') --pattern $pattern } }

$engineArguments = @{}
if ($EngineRoot) { $engineArguments.EngineRoot = $EngineRoot }
if ($source.Count) { & (Join-Path $PSScriptRoot 'BuildEditor.ps1') @engineArguments }
if ($Filter) {
    # A new worktree holds only Git LFS pointers; the editor then fails on every asset with an unrelated error.
    $asset = Get-ChildItem -LiteralPath (Join-Path $root 'Content') -Recurse -File -Filter '*.uasset' | Select-Object -First 1
    if ($asset) {
        $head = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($asset.FullName), 0, [Math]::Min(7, [int]$asset.Length))
        if ($head -eq 'version') { throw 'Content holds Git LFS pointers, not assets. Run "git lfs pull" before the rule tests.' }
    }
    & (Join-Path $PSScriptRoot 'RunTests.ps1') @engineArguments -Filter $Filter
}
Write-Host 'Verify passed.'
