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
    Host = @('Net','Start','Countdown','Opening','Fixture','A14','N03','Iso','DevTest')
    Client1 = @('Net','Start','Countdown','Opening','Fixture','N03','A01','A06','A10','A12','A14','A24','Iso','DevTest')
}

function Start-GrowthRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view and the DEV growth fixture that presets the owning
    # client's offers and grants its own XP; the owning client never carries the fixture and renders offscreen.
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
    # D10: each owner opened both matches with exactly one pending opening choice; the client's first was its staged preset.
    foreach ($role in $roles) {
        foreach ($field in @('opening','second_opening')) {
            $row = $reports[$role].$field
            if ($null -eq $row -or [long]$row.pending -ne 1 -or [long]$row.earned -ne 1 -or [long]$row.consumed -ne 0 -or [long]$row.ordinal -ne 1) {
                throw "$role did not open its $field match with exactly one pending opening choice: $output"
            }
        }
        if ($reports[$role].opening.offer -eq $reports[$role].second_opening.offer) { throw "$role's second match reused its opening offer: $output" }
    }
    if (-not $reports['Client1'].opening.staged_preset) { throw "The client's opening offer is not the Host's staged preset: $output" }
    # #153: the remote owning connection had its move, Shift step and Q refused during the opening countdown.
    if (-not $reports['Client1'].countdown.refusals_checked -or [long]$reports['Client1'].countdown.started_at_ms -ne [long]$reports['Host'].countdown.started_at_ms) {
        throw "The client's countdown refusals or the shared T=0 are missing from the reports: $output"
    }
    $hostReport = $reports['Host']
    $client = $reports['Client1']
    if (-not $hostReport.growth_fixture -or $client.growth_fixture -or -not $hostReport.buyer_killed) {
        throw "Only the Host may run the DEV growth fixture, and it must have killed the owning client: $output"
    }
    # The Listen Host as killer: its contender and camp XP rewards reached its own view and HUD (the probe's N03 checks).
    if ([long]$hostReport.host_killer.contender_xp -le 0 -or [long]$hostReport.host_killer.camp_kills -ne 2 -or
        [long]$hostReport.host_killer.camp_xp -le 0) {
        throw "The Host's own kill rewards are missing from its report: $output"
    }
    if (-not $client.replacement.track -or $client.replacement.track -ne $client.replacement.relearned_track -or
        [long]$client.replacement.ready_at_ms -ne [long]$client.replacement.relearned_ready_at_ms -or
        [long]$client.dead_choice.max_hp_p2_after -le [long]$client.dead_choice.max_hp_p2_before -or
        [long]$client.dead_choice.respawned_life -ne [long]$client.dead_choice.life + 1) {
        throw "The client's replacement or choice while dead is missing from its report: $output"
    }
    if ($client.match1.generation -ne $hostReport.match1.generation -or $client.second_generation -ne $hostReport.second_generation -or
        $client.generation -ne $client.second_generation -or $hostReport.generation -ne $hostReport.second_generation -or
        $client.second_generation -le $client.match1.generation) {
        throw "Roles did not observe the same first and second match generations: $output"
    }
    if (@($client.dev_test).Count -ne 13 -or @($hostReport.dev_test).Count -ne 3) {
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
        "stages the client's offers in the Lobby) plus one owning client; each owner opened each match with exactly one pending opening " +
        "choice, never re-granted by a respawn; level $($client.match1.level) from $($client.kills) kills; Q's cooling rank-2 skill " +
        "replaced after a refused unconfirmed pick, relearned at rank 1 on its old track and deadline; after the Host's own DevFixture " +
        "XP grant it killed the client, which chose while dead without revive or heal and respawned full; the Host's own kill and two " +
        "low-camp last hits paid their exact XP rewards into its own view and rendered HUD. The Host then ended the match " +
        "(no winner) and returned to Lobby generation $($client.second_generation): a second normal match started both owners at clean " +
        "growth and cooldown state and refused the first match's choice and receipt query. DEV test commands: the client through its " +
        "remote owner RPC (stale match, replay and conflict refused or answered without a second commit; cooldown reset, refill, learn " +
        "on the open opportunity, learn without one refused, a DEV AD term shown as its own row and cleared, a reset to the match " +
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
