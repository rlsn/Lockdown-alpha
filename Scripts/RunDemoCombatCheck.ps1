[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,1500)][int]$TimeoutSeconds=600,
    # Optional engine packet simulation for every role, e.g. '-PktLag=40 -PktLagVariance=10 -PktLoss=1'.
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation,
    # The Host's rendered main view; the screenshots are reviewed at both supported sizes.
    [ValidateSet('720p','1080p')][string]$Resolution='720p',
    [switch]$RenderClient,
    # Audio/feel evidence (Scripts/CaptureFeelSample.ps1 -Scenario combat): the Host window is visible with sound on and
    # logs every played sound; the checks themselves are unchanged.
    [switch]$Audible)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory ($(if ($PackageRoot) { 'DemoCombat-OwnedOrders-Packaged' } else { 'DemoCombat-OwnedOrders' }) +
    $(if ($NetEmulation) { '-NetEmulated' } else { '' }) + $(if ($Resolution -ne '720p') { "-$Resolution" } else { '' }))
$resolutionArguments = if ($Resolution -eq '1080p') { '-ForceRes -ResX=1920 -ResY=1080' } else { '-ResX=1280 -ResY=720' }
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds) -PollMilliseconds 200 `
    -ExitError { param($process) "Combat scenario process exited before its evidence completed: PID $($process.Id). Evidence: $output" } `
    -TimeoutError { "Combat scenario evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire
$roles = @('Host','Client1')
# Every role must report each check group it owns; a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @('Net','Start','ChaseGoal','AttackMove','Stop','Death','Respawn','Protection','Skills','CastFollowUp','Render','Feel','Pointer')
    Client1 = @('Net','Start','Settings','AttackMove','Stop','Death','DeathCamera','Respawn','Protection','Skills','CastFollowUp','Feel','Queue')
}
# Only the Host runs the DEV growth fixture: it presets offers and grants XP on its authority, with no RPC.
$fixtureSwitch = '-LDDevGrowthFixture'
# The Host's rendered captures of its own body and the observed skill moments, kept for presentation review and never
# pixel-compared. The first (the duel site) also absorbs the stall of the process's first read-back.
$screenshots = @('HostSees-DuelSite.png','HostSees-MoveMarker.png','HostSees-OwnHit.png','HostSees-Hover.png',
    'HostSees-AttackMarker.png','HostSees-Root.png','HostSees-Shield.png','HostSees-ChalkFlight.png','HostSees-OwnStrike.png')

function Start-CombatRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        # The Host always renders. RenderClient also exercises the cooked skeletal data on the real owning client.
        $render = if ($Role -eq 'Host') { "-windowed $(if ($Audible) { '' } else { '-RenderOffScreen' }) $resolutionArguments -port=$port $fixtureSwitch" }
            elseif ($RenderClient) { "-windowed -RenderOffScreen $resolutionArguments" } else { '-nullrhi' }
        $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCombatProbe -LDDemoCombatRole={2} -LDDemoCombatTimeout={3} -LDDemoCombatOutput="{4}" -abslog="{5}"' -f $render,$user,$Role,$TimeoutSeconds,$evidence,$log
        if ($NetEmulation) { $arguments += " $NetEmulation" }
        if ($Audible -and $Role -eq 'Host') { $arguments = $arguments.Replace(' -nosound', '') + ' -LDAudioEventLog' }
        $arguments
    }
}

try {
    Start-CombatRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCombatHost'
    Wait-LDDemoEvidence -Harness $harness -Names @('HostReady.json')
    $url = '127.0.0.1:{0}?Name=DemoCombatClient1?LDWire={1}?LDProduct={2}?LDCatalog={3}' -f $port,$wire,$version,$digest
    Start-CombatRole 'Client1' $url
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json')
    $reports = @{}
    foreach ($role in $roles) {
        $report = Read-LDDemoReport -Harness $harness -Name "$role.json"
        $failed = @($report.checks | Where-Object { -not $_.passed })
        if (-not $report.passed -or $report.role -ne $role -or -not $report.dev_probe -or -not $report.normal_start -or
            $report.catalog_sha256 -ne $digest -or $failed.Count -ne 0) {
            throw "Owned combat evidence failed for $role`: $($report.detail) Evidence: $output"
        }
        foreach ($id in $requiredChecks[$role]) {
            if (@($report.checks | Where-Object { $_.id -eq $id }).Count -eq 0) { throw "$role report lacks its $id checks: $output" }
        }
        $reports[$role] = $report
    }
    $hostReport = $reports['Host']
    # #198: life 1 is the R0 rest body, life 2 the first field life the Host kills; Client1 finishes on life 3.
    if ($reports['Client1'].generation -ne $hostReport.generation -or $reports['Client1'].life -ne 3) {
        throw "Roles observed different matches or Client1 did not finish on its respawned field life (life 3): $output"
    }
    if ($hostReport.growth_fixture -ne $true -or $reports['Client1'].growth_fixture -ne $false) {
        throw "Only the Host may run with the DEV growth fixture: $output"
    }
    foreach ($name in $screenshots) {
        $image = Join-Path $evidence $name
        if (@($hostReport.screenshots | Where-Object { $_.file -eq $name }).Count -ne 1 -or
            -not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).Length -le 0) {
            throw "The participating Listen Host left no rendered capture $name of its observed skill moment: $output"
        }
    }
    Stop-LDDemoClients -Harness $harness -Message 'Disconnect owning clients' -ExitError "Owning client did not exit cleanly: $output"
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    if (-not (Select-String -LiteralPath (Join-Path $output 'Host.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE' -Quiet) -or
        (Select-String -LiteralPath (Join-Path $output 'Client1.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE' -Quiet)) {
        throw "The DEV growth fixture must be active on the Host and absent from Client1: $output"
    }
    # The final report names every scenario that ran a DEV fixture.
    [ordered]@{ fixture = 'growth'; switch = $fixtureSwitch; roles = @('Host') } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $output 'DevFixture.json') -Encoding UTF8
    Assert-LDNetEmulationApplied -Output $output -Roles $roles -NetEmulation $NetEmulation
    $impairment = if ($NetEmulation) { " Every role ran under engine packet simulation ($NetEmulation) on one machine's loopback." } else { '' }
    # Packaged evidence moves into $output when the run ends, so these are the captures' final paths in both modes.
    $captures = ($screenshots | ForEach-Object { Join-Path $output $_ }) -join ', '
    $hostFeel = $hostReport.feel
    $clientFeel = $reports['Client1'].feel
    $feel = ("Control feel (measured, ms): Host right-click to motion $($hostFeel.click_to_move_ms) " +
        "($($hostFeel.click_frames) frames, longest $($hostFeel.click_longest_frame_ms); walked $($hostFeel.spawn_to_click_ms) after spawn once frames settled), ramp to full speed " +
        "$($hostFeel.ramp_to_full_speed_ms), S from $([math]::Round($hostFeel.stop_from_speed)) cm/s to still $($hostFeel.stop_to_still_ms) " +
        "over $([math]::Round($hostFeel.stop_distance_cm)) cm, next walk $($hostFeel.restart_click_to_move_ms) (frame $([math]::Round($hostFeel.frame_ms,1))); " +
        "Client1 right-click to Activated receipt $($clientFeel.click_to_activated_receipt_ms), to motion $($clientFeel.click_to_move_ms) " +
        "($($clientFeel.click_frames) frames, longest $($clientFeel.click_longest_frame_ms)), " +
        "ramp $($clientFeel.ramp_to_full_speed_ms), S to Stopped receipt $($clientFeel.stop_to_stopped_receipt_ms) " +
        "(frame $([math]::Round($clientFeel.frame_ms,1))); acceleration $($hostFeel.max_acceleration), braking " +
        "$($hostFeel.braking_deceleration_walking), speed $($hostFeel.max_walk_speed) cm/s.")
    Write-Host $feel
    Write-Host ("DEV-only owned-connection combat/orders scenario passed through the ordinary lobby Start: a chased body's " +
        "foot in the navmesh margin routes and the same clicked ground projects onto the navmesh (#80), Settings held " +
        "over an accepted walk past its lease (order and heartbeat continue, nothing replays), remote attack-move " +
        "and S, right-click kill with kill-feed credit, a minimap pan of the static death camera, respawn at spawn anchor " +
        "$($hostReport.respawn_anchor) with exact protection, protection spent by an empty Q; then actives learned by ordinary " +
        "build commands on DEV-fixture-preset offers: active_pants_down root observed, active_not_mad shield and dispel, " +
        "Client1's active_snark_bolt hitting and slowing the Host ($($hostReport.projectile_samples) flight samples), Host and Client1 " +
        "cast follow-up and refused-cast tracking, a basic hit after a chase " +
        "at $($hostReport.reach_hit_cm) cm.$impairment " +
        "DEV growth fixture: Host only ($fixtureSwitch). Rendered Host captures for presentation review (not pixel-compared): " +
        "$captures. Not camps, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    Close-LDDemoHarness -Harness $harness
}
