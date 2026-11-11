[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,2400)][int]$TimeoutSeconds=1500)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoGrowth-Owned-Packaged' } else { 'DemoGrowth-Owned' })
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
# Every role must report the check groups it owns (PROGRESSION_AND_REWARDS §12 ids); a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @('Net','Start','Rest0','Rest','Opening','Fixture','A14','N03','Iso','DevTest')
    Client1 = @('Net','Start','Rest0','Rest','Opening','Fixture','N03','A01','A06','A10','A12','A14','A15','A16','A24','Iso','DevTest')
}

function Start-GrowthRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view and the DEV growth fixture that presets the owning
    # client's active offers and grants its own XP; the owning client never carries the fixture and renders offscreen.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port -LDDevGrowthFixture" } else { '-windowed -RenderOffScreen -ResX=1280 -ResY=720' }
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoGrowthProbe -LDDemoGrowthRole={2} -LDDemoGrowthTimeout={3} -LDDemoGrowthOutput="{4}" -abslog="{5}"' -f $render,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Wait-GrowthFiles {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Growth scenario process exited before its evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Growth scenario evidence timed out: $output"
}

function Read-GrowthJson {
    param([string]$Name)
    Get-Content -LiteralPath (Join-Path $evidence $Name) -Raw | ConvertFrom-Json
}

try {
    Start-GrowthRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoGrowthHost'
    Wait-GrowthFiles @('HostReady.json')
    $url = '127.0.0.1:{0}?Name=DemoGrowthClient1?LDWire={1}?LDProduct={2}?LDCatalog={3}' -f $port,$wire,$version,$digest
    Start-GrowthRole 'Client1' $url
    Wait-GrowthFiles @('Host.json','Client1.json')
    $reports = @{}
    $failures = @()
    foreach ($role in $roles) {
        $report = Read-GrowthJson "$role.json"
        $failed = @($report.checks | Where-Object { -not $_.passed })
        if (-not $report.passed -or $report.role -ne $role -or -not $report.dev_probe -or
            $report.catalog_sha256 -ne $digest -or $failed.Count -ne 0) {
            $failures += "$role`: $($report.detail)"
        }
        $reports[$role] = $report
    }
    if ($failures.Count) { throw "Owned growth evidence failed: $($failures -join ' | ') Evidence: $output" }
    foreach ($role in $roles) {
        foreach ($id in $requiredChecks[$role]) {
            if (@($reports[$role].checks | Where-Object { $_.id -eq $id }).Count -eq 0) { throw "$role report lacks its $id checks: $output" }
        }
    }
    # D10: each owner opened both matches with exactly one queued opening opportunity and its unspent opening points; the
    # client's first opening offer was its staged preset.
    foreach ($role in $roles) {
        foreach ($field in @('opening','second_opening')) {
            $row = $reports[$role].$field
            if ($null -eq $row -or [long]$row.queued -ne 1 -or [long]$row.earned -ne 1 -or [long]$row.consumed -ne 0 -or
                [long]$row.ordinal -ne 1 -or [long]$row.unspent_points -le 0) {
                throw "$role did not open its $field match with exactly one queued opening opportunity and its opening points: $output"
            }
        }
        if ($reports[$role].opening.offer -eq $reports[$role].second_opening.offer) { throw "$role's second match reused its opening offer: $output" }
    }
    if (-not $reports['Client1'].opening.staged_preset) { throw "The client's opening offer is not the Host's staged preset: $output" }
    # #198: in the opening rest R0 the remote owning connection learned its opening active into Q and allocated every opening
    # point in the build step, then had its attack-move, Shift step and Q refused in the return countdown; both roles saw the
    # same match start and field 1 start (T=0).
    $rest0 = $reports['Client1'].rest0
    $hostRest0 = $reports['Host'].rest0
    if (-not $rest0.refusals_checked -or [long]$rest0.started_at_ms -ne [long]$hostRest0.started_at_ms -or
        [long]$rest0.field_started_at_ms -ne [long]$hostRest0.field_started_at_ms -or
        [long]$rest0.field_started_at_ms -le [long]$rest0.started_at_ms -or
        -not $rest0.opening_active -or [long]$rest0.opening_slot -ne 0 -or
        [long]$rest0.points_allocated -ne [long]$reports['Client1'].opening.unspent_points -or
        [long]$rest0.max_hp_p2_after -le [long]$rest0.max_hp_p2_before) {
        throw "The client's R0 refusals, opening learn, opening allocation or the shared field 1 start are missing from the reports: $output"
    }
    $hostReport = $reports['Host']
    $client = $reports['Client1']
    # #198 mid rests: the first match crosses at least R1 for both roles; every rest retired the field life without a death
    # (public death count unchanged), placed the rest life and returned the next field life, with T frozen at the segment end.
    foreach ($role in $roles) {
        $rests = @($reports[$role].rests)
        if ($rests.Count -lt 1) { throw "$role crossed no mid rest in the first match: $output" }
        foreach ($rest in $rests) {
            if ([long]$rest.deaths_after -ne [long]$rest.deaths_before -or [long]$rest.rest_life -le [long]$rest.field_life_before -or
                [long]$rest.field_life_after -ne [long]$rest.rest_life + 1 -or [long]$rest.rest -lt 1 -or [long]$rest.rest -gt 3) {
                throw "$role's rest R$($rest.rest) counted a death or skipped its rest life: $output"
            }
        }
    }
    if (-not $hostReport.growth_fixture -or $client.growth_fixture -or -not $hostReport.buyer_killed) {
        throw "Only the Host may run the DEV growth fixture, and it must have killed the owning client: $output"
    }
    # The Listen Host as killer: its contender and camp XP rewards reached its own view and HUD (the probe's N03 checks).
    if ([long]$hostReport.host_killer.contender_xp -le 0 -or [long]$hostReport.host_killer.camp_kills -ne 2 -or
        [long]$hostReport.host_killer.camp_xp -le 0) {
        throw "The Host's own kill rewards are missing from its report: $output"
    }
    # #222 build at all times: the level offer chosen in combat is learned into W at once and the points allocated in the same
    # combat; while dead the next choice is learned into E and points allocated without revive, and E is still there after
    # the respawn.
    $window = $client.window
    if ([long]$window.learned_slot -ne 1 -or [long]$window.combat_until_ms -ge [long]$window.segment_ends_at_ms -or
        [long]$window.points_allocated -le 0 -or [long]$window.max_hp_p2_after -le [long]$window.max_hp_p2_before) {
        throw "The client's in-combat learn and allocation are missing from its report: $output"
    }
    $dead = $client.dead_choice
    if (-not $dead.learned_while_dead -or [long]$dead.points_allocated -le 0 -or
        [long]$dead.max_hp_p2_after -le [long]$dead.max_hp_p2_before -or
        [long]$dead.respawned_life -ne [long]$dead.life + 1 -or [long]$dead.learned_slot -ne 2) {
        throw "The client's build commands while dead or its E after the respawn are missing from its report: $output"
    }
    if (-not $hostReport.opponent_view_checked) { throw "The Host never checked its opponent view of the buyer: $output" }
    if ($client.match1.generation -ne $hostReport.match1.generation -or $client.second_generation -ne $hostReport.second_generation -or
        $client.generation -ne $client.second_generation -or $hostReport.generation -ne $hostReport.second_generation -or
        $client.second_generation -le $client.match1.generation) {
        throw "Roles did not observe the same first and second match generations: $output"
    }
    if (@($client.dev_test).Count -ne 15 -or @($hostReport.dev_test).Count -ne 3) {
        throw "A DEV test step is missing from the reports: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    if (-not $processes[1].WaitForExit(30000) -or $processes[1].ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    if (-not (Select-String -LiteralPath (Join-Path $output 'Host.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE' -Quiet) -or
        (Select-String -LiteralPath (Join-Path $output 'Client1.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE' -Quiet)) {
        throw "LD_DEV_FIXTURE_ACTIVE must be logged by the Host and never by the owning client: $output"
    }
    Write-Host ("DEV-only owned-connection growth scenario passed: rendered participating Listen Host (victim, -LDDevGrowthFixture " +
        "stages the client's active offers in the Lobby) plus one owning client; each owner opened each match with exactly one queued " +
        "opening opportunity and its opening points, never re-granted by a respawn; the client learned its opening active into Q and " +
        "allocated its opening points in the opening rest R0; level $($client.match1.level) from $($client.kills) kills across " +
        "$(@($client.rests).Count) mid rest(s), each retiring the field life without a death and returning the next field life with " +
        "field T frozen through it (both roles hold through every rest; the client never died before it asked to); after the kill " +
        "reaching the first offer level its choice was learned into W and every point allocated while its combat window still ran; " +
        "after the Host's own DevFixture XP grant it killed the client, which learned its next choice into E and allocated points " +
        "while dead without revive or heal, then respawned full with E learned; the Host's own kill and two low-camp last " +
        "hits paid their exact XP rewards into its own view and rendered HUD. The Host then ended the match (no winner) and returned to " +
        "Lobby generation $($client.second_generation): a second normal match started both owners at clean growth, point and cooldown " +
        "state and refused the first match's choice and receipt query. DEV test commands: the client through its remote owner RPC " +
        "(stale match, replay and conflict refused or answered without a second commit; cooldown reset, refill, a DEV learn and its " +
        "refused repeat, a DEV allocation and its refused overdraw, a DEV AD term shown as its own row and cleared, a reset to the match " +
        "start, a match-wide camp refresh refused) and the Host through its own as Listen Host (camp refresh, early capture opening). " +
        "Not camp lifecycle, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
