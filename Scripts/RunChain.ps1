# One command for "build -> the named rule tests -> the named scenarios". Stops at the first failing step and prints a
# summary of every step with its time and evidence; a partial run like this is never a gate.
#   .\Scripts\RunChain.ps1 -Filter Lockdown.Rules.DemoBodyMotion.HitLayers -Scenario Vision,Combat
#   .\Scripts\RunChain.ps1 -Scenario OpponentUI -NoBuild
[CmdletBinding()]
param([string]$EngineRoot,
    # Rule tests to run after the build (RunTests.ps1 -Filter); none = skip the rule-test step.
    [string[]]$Filter,
    # Scenarios to run in order, each through its own Run*Check.ps1.
    [ValidateSet('Core','Combat','Camp','Growth','Capture','Vision','VisionReaction','Entry','Frontend','WorldRoom','SessionFault',
        'Opponent','OpponentUI','Telemetry','CombatVfx')][string[]]$Scenario,
    # Skip the editor build (the binaries are already current).
    [switch]$NoBuild)
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $NoBuild -and -not $Filter -and -not $Scenario) { Write-Host 'Nothing but the build was asked for; add -Filter and/or -Scenario.' }
$engine = @{}
if ($EngineRoot) { $engine.EngineRoot = $EngineRoot }
$scripts = @{
    Core = @('RunDemoCoreCheck.ps1', @{}); Combat = @('RunDemoCombatCheck.ps1', @{}); Camp = @('RunDemoCampCheck.ps1', @{})
    Growth = @('RunDemoGrowthCheck.ps1', @{}); Capture = @('RunDemoCaptureCheck.ps1', @{}); Vision = @('RunDemoVisionCheck.ps1', @{})
    VisionReaction = @('RunDemoVisionCheck.ps1', @{ReactionOnly = $true}); Entry = @('RunDemoEntryCheck.ps1', @{})
    Frontend = @('RunDemoFrontendCheck.ps1', @{}); WorldRoom = @('RunDemoWorldRoomCheck.ps1', @{})
    SessionFault = @('RunDemoSessionFaultCheck.ps1', @{}); Opponent = @('RunDemoOpponentCheck.ps1', @{})
    OpponentUI = @('RunDemoOpponentCheck.ps1', @{UIOnly = $true}); Telemetry = @('RunDemoOpponentCheck.ps1', @{Telemetry = $true; Accelerated = $true})
    CombatVfx = @('CaptureDemoArt.ps1', @{CombatVfx = $true})
}
$steps = [System.Collections.Generic.List[object]]::new()
if (-not $NoBuild) { $steps.Add(@{Name = 'Build'; Script = 'BuildEditor.ps1'; Arguments = @{}}) }
if ($Filter) { $steps.Add(@{Name = 'Rules'; Script = 'RunTests.ps1'; Arguments = @{Filter = $Filter; ImportTests = 'Run'}}) }
foreach ($name in $Scenario) { $steps.Add(@{Name = $name; Script = $scripts[$name][0]; Arguments = $scripts[$name][1]}) }

$results = [System.Collections.Generic.List[object]]::new()
$failed = $null
foreach ($step in $steps) {
    $arguments = $engine + $step.Arguments
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $status = 'passed'; $detail = ''
    Write-Host "== $($step.Name): $($step.Script)"
    try {
        # *>&1: the evidence lines are Write-Host output (the information stream).
        $lines = & (Join-Path $PSScriptRoot $step.Script) @arguments *>&1 | ForEach-Object { Write-Host $_; "$_" }
        $detail = @($lines | Where-Object { $_ -match 'Evidence: |inspect visually: ' } | Select-Object -Last 1) -join ''
    } catch {
        $status = 'FAILED'; $detail = "$($_.Exception.Message)"; $failed = $step.Name
    }
    $results.Add([pscustomobject]@{Step = $step.Name; Result = $status; Seconds = [int]$watch.Elapsed.TotalSeconds; Detail = $detail})
    if ($failed) { break }
}
foreach ($step in $steps | Select-Object -Skip $results.Count) {
    $results.Add([pscustomobject]@{Step = $step.Name; Result = 'not run'; Seconds = 0; Detail = "stopped after $failed failed"})
}
Write-Host ''
Write-Host 'Chain summary (a partial local run, not a gate):'
$results | Format-Table -AutoSize -Wrap | Out-String -Width 200 | Write-Host
if ($failed) { throw "Chain stopped at $failed." }
