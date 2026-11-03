[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,1500)][int]$TimeoutSeconds=600,
    # Optional engine packet simulation for every role, e.g. '-PktLag=40 -PktLagVariance=10 -PktLoss=1'.
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory ($(if ($PackageRoot) { 'DemoCombat-OwnedOrders-Packaged' } else { 'DemoCombat-OwnedOrders' }) +
    $(if ($NetEmulation) { '-NetEmulated' } else { '' }))
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
# Probe markers and reports live where the probe accepts them; logs and user dirs stay beside the runner.
$evidence = $launch.ProbeOutput
$socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
$port = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$projectRoot = Get-LDProjectRoot
$digest = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$version = Get-LDProjectVersion
# The wire version is read from the native source, so this runner cannot drift from the build it launches.
$entryHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'Source\Lockdown\Public\Framework\LDDemoEntry.h') -Raw
if ($entryHeader -notmatch 'inline\s+constexpr\s+uint32\s+WireProtocol\s*=\s*(\d+)\s*;') { throw 'Cannot read WireProtocol from LDDemoEntry.h' }
$wire = [int]$Matches[1]
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$roles = @('Host','Client1')
# Every role must report each check group it owns; a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @('Net','Start','ChaseGoal','AttackMove','Stop','Death','Respawn','Protection','Skills','Render','Feel','Pointer')
    Client1 = @('Net','Start','Settings','AttackMove','Stop','Death','DeathCamera','Respawn','Protection','Skills','Feel')
}
# Only the Host runs the DEV growth fixture: it presets offers and grants XP on its authority, with no RPC.
$fixtureSwitch = '-LDDevGrowthFixture'
# The Host's rendered captures of its own body and the observed skill moments, kept for presentation review and never
# pixel-compared. The first (the duel site) also absorbs the stall of the process's first read-back.
$screenshots = @('HostSees-DuelSite.png','HostSees-MoveMarker.png','HostSees-OwnHit.png','HostSees-Hover.png',
    'HostSees-AttackMarker.png','HostSees-Root.png','HostSees-Shield.png','HostSees-ChalkFlight.png','HostSees-OwnStrike.png')

function Start-CombatRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view; the owning client needs no renderer.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port $fixtureSwitch" } else { '-nullrhi' }
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCombatProbe -LDDemoCombatRole={2} -LDDemoCombatTimeout={3} -LDDemoCombatOutput="{4}" -abslog="{5}"' -f $render,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    if ($NetEmulation) { $arguments += " $NetEmulation" }
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Wait-CombatFiles {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Combat scenario process exited before its evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Combat scenario evidence timed out: $output"
}

try {
    Start-CombatRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCombatHost'
    Wait-CombatFiles @('HostReady.json')
    $url = '127.0.0.1:{0}?Name=DemoCombatClient1?LDWire={1}?LDProduct={2}?LDCatalog={3}' -f $port,$wire,$version,$digest
    Start-CombatRole 'Client1' $url
    Wait-CombatFiles @('Host.json','Client1.json')
    $reports = @{}
    foreach ($role in $roles) {
        $report = Get-Content -LiteralPath (Join-Path $evidence "$role.json") -Raw | ConvertFrom-Json
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
    if ($reports['Client1'].generation -ne $hostReport.generation -or $reports['Client1'].life -ne 2) {
        throw "Roles observed different matches or Client1 did not finish on its second life: $output"
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
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    if (-not $processes[1].WaitForExit(30000) -or $processes[1].ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
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
    $feel = ("Control feel (measured, ms): Host right-click to motion $($hostFeel.click_to_move_ms), ramp to full speed " +
        "$($hostFeel.ramp_to_full_speed_ms), S from $([math]::Round($hostFeel.stop_from_speed)) cm/s to still $($hostFeel.stop_to_still_ms) " +
        "over $([math]::Round($hostFeel.stop_distance_cm)) cm, next walk $($hostFeel.restart_click_to_move_ms) (frame $([math]::Round($hostFeel.frame_ms,1))); " +
        "Client1 right-click to Activated receipt $($clientFeel.click_to_activated_receipt_ms), to motion $($clientFeel.click_to_move_ms), " +
        "ramp $($clientFeel.ramp_to_full_speed_ms), S to Stopped receipt $($clientFeel.stop_to_stopped_receipt_ms) " +
        "(frame $([math]::Round($clientFeel.frame_ms,1))); acceleration $($hostFeel.max_acceleration), braking " +
        "$($hostFeel.braking_deceleration_walking), speed $($hostFeel.max_walk_speed) cm/s.")
    Write-Host $feel
    Write-Host ("DEV-only owned-connection combat/orders scenario passed through the ordinary lobby Start: a chased body's " +
        "foot in the navmesh margin routes while the same clicked ground stays refused, Settings held " +
        "over an accepted walk past its lease (order and heartbeat continue, nothing replays), remote attack-move " +
        "and S, right-click kill with kill-feed credit, a minimap pan of the static death camera, respawn at spawn anchor " +
        "$($hostReport.respawn_anchor) with exact protection, protection spent by an empty Q; then skills learned by ordinary " +
        "choices of DEV-fixture-preset offers: active_binding_snare root observed, active_guard_pulse shield and dispel, " +
        "Client1's active_arc_bolt hitting the Host ($($hostReport.projectile_samples) flight samples), an attr_reach basic " +
        "hit at $($hostReport.reach_hit_cm) cm.$impairment " +
        "DEV growth fixture: Host only ($fixtureSwitch). Rendered Host captures for presentation review (not pixel-compared): " +
        "$captures. Not gear actives, camps, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
