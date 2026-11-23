[CmdletBinding()]
# Short owned-connection growth scenario: one rendered Listen Host and one headless owning client, no AI. State is set
# with DEV test commands over each role's own connection; the probe (LDDemoGrowthLinkProbe.cpp) checks invariants only.
# -TimeoutSeconds bounds each role's probe; a passing run takes well under three minutes including start-up.
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(60,600)][int]$TimeoutSeconds=240)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoGrowthLink-Owned-Packaged' } else { 'DemoGrowthLink-Owned' })
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$evidence = $launch.ProbeOutput
$roles = @('Host','Client1')

# A role that fails a check writes its report and exits; name that check instead of the bare exit.
function Get-GrowthLinkFailure {
    foreach ($role in $roles) {
        $file = Join-Path $evidence "$role.json"
        if (Test-Path -LiteralPath $file) {
            $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (-not $report.passed) { return "$role reported: $($report.detail)" }
        }
    }
    return 'no role report explains it'
}

$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds + 60) -PollMilliseconds 200 `
    -ExitError { param($process) "Growth link scenario process exited before its evidence completed: PID $($process.Id); $(Get-GrowthLinkFailure). Evidence: $output" } `
    -TimeoutError { "Growth link scenario evidence timed out: $(Get-GrowthLinkFailure). Evidence: $output" }
# Every role must report the checks it owns, so a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @(
        'The grant''s XP, level and active opportunity appear in the owner''s own view',
        'The Host''s rendered HUD level badge shows the level of its own view',
        'The Host''s own life ended under the client''s attack',
        'The client''s own view XP after the kill equals the authority''s record for that contender')
    Client1 = @(
        'The DEV XP grant reaches the server and its receipt returns Applied',
        'The grant''s XP, level and active opportunity appear in the owner''s own view',
        'The client''s SubmitBuild active choice returns an Applied receipt',
        'The chosen active appears among the learned actives of the client''s own view',
        'The client''s combat projection carries the chosen active as an entry',
        'The client''s SubmitBuild point allocation returns an Applied receipt',
        'The allocated stat''s points rise in the client''s own view',
        'The client''s combat projection follows the allocation with a larger maximum HP',
        'The client''s own attack order ended with the Host''s death',
        'The kill raises the XP in the client''s own view')
}

function Start-GrowthLinkRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        # The Host renders (1280x720 offscreen) so its HUD is real, and carries the DEV growth fixture that admits DEV test
        # commands on this authority; the owning client is headless and never carries the fixture.
        $mode = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$($harness.Port) -LDDevGrowthFixture" } else { '-nullrhi' }
        '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoGrowthLinkProbe -LDDemoGrowthLinkRole={2} -LDDemoGrowthLinkTimeout={3} -LDDemoGrowthLinkOutput="{4}" -abslog="{5}"' -f $mode,$user,$Role,$TimeoutSeconds,$evidence,$log
    }
}

try {
    Start-GrowthLinkRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=GrowthLinkHost'
    Wait-LDDemoEvidence -Harness $harness -Names @('HostJoined.json')
    $url = '127.0.0.1:{0}?Name=GrowthLinkClient1?LDWire={1}?LDProduct={2}?LDCatalog={3}' -f $harness.Port,$harness.Wire,$harness.Version,$harness.Digest
    Start-GrowthLinkRole 'Client1' $url
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json')
    $reports = @{}
    foreach ($role in $roles) {
        $report = Read-LDDemoReport -Harness $harness -Name "$role.json"
        $failed = @($report.checks | Where-Object { -not $_.passed })
        foreach ($check in $failed) { Write-Host "$role FAILED: $($check.name) -- $($check.detail)" }
        if (-not $report.passed -or $report.role -ne $role -or $failed.Count) {
            throw "Growth link scenario failed for $role`: $($report.detail). Evidence: $output"
        }
        foreach ($name in $requiredChecks[$role]) {
            if (@($report.checks | Where-Object { $_.name -eq $name }).Count -eq 0) { throw "$role's report lacks the check '$name'. Evidence: $output" }
        }
        $reports[$role] = $report
    }
    if (-not $reports['Host'].growth_fixture -or $reports['Client1'].growth_fixture) {
        throw "Only the Host may run the DEV growth fixture. Evidence: $output"
    }
    if ($reports['Host'].generation -ne $reports['Client1'].generation) { throw "The roles did not play the same match generation. Evidence: $output" }
    Stop-LDDemoClients -Harness $harness -Message 'Disconnect owning client' -ExitError "Owning client did not exit cleanly: $output"
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Growth link scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    $client = $reports['Client1'].facts
    Write-Host ("DEV-only short owned-connection growth scenario passed in $([int]$watch.Elapsed.TotalSeconds) s of wall time including start-up: " +
        "a rendered Listen Host (-LDDevGrowthFixture) and one headless owning client, no AI. The client's DEV XP grant, active choice " +
        "($($client.chosen_active)) and point allocation each returned an Applied receipt and showed in its own view and combat " +
        "projection; its own attack order killed the Host ($($client.attack_orders) order(s), $([math]::Round($client.kill_seconds, 1)) s) " +
        "and its view XP after the kill ($($client.xp_after_kill)) equals the authority's record; the Host's rendered HUD level badge " +
        "matches its own view. Invariants only: no expected number is recomputed. Not rests, respawn, camps, a second match, AI or " +
        "capture. Evidence: $output")
}
finally {
    Close-LDDemoHarness -Harness $harness
}
