[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,2400)][int]$TimeoutSeconds=1500)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoGrowth-OwnedShop-Packaged' } else { 'DemoGrowth-OwnedShop' })
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
$checker = Join-Path $PSScriptRoot 'CheckEconomyEvidence.py'
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$roles = @('Host','Client1')
# Every role must report the check groups it owns (PROGRESSION_EQUIPMENT_ECONOMY §12 ids); a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @('Net','Start','Opening','Ledger','Fixture','A14','N03','Iso','DevTest')
    Client1 = @('Net','Start','Opening','Fixture','N03','A01','A06','A10','A12','A14','A15','A16','A17','A19','A21','A24','Iso','DevTest')
}

function Start-GrowthRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view and the DEV growth fixture that presets the buyer's
    # offers and grants its own XP and gold; the owning client needs no renderer and never carries the fixture.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port -LDDevGrowthFixture" } else { '-nullrhi' }
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

# The independent replay of the exported ledger; a non-zero exit fails the run. The Host ran with the DEV growth fixture,
# so its DevFixture mints are accepted only because they are declared here and listed in the evidence.
function Assert-LedgerEvidence {
    param([string]$Name)
    Invoke-LDTool -Tool 'python' -ToolArguments @('-B', $checker, '--dev-fixture', (Join-Path $evidence $Name)) -LogPath (Join-Path $output "$Name.check.txt")
}

function Get-ItemKeys {
    param($Items)
    @(@($Items) | Where-Object { $null -ne $_ } | ForEach-Object { '{0}|{1}|{2}' -f $_.instance, $_.definition, $_.slot } | Sort-Object)
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
    # D10: each owner opened both matches with exactly one pending opening choice; the buyer's first was its staged preset.
    foreach ($role in $roles) {
        foreach ($field in @('opening','second_opening')) {
            $row = $reports[$role].$field
            if ($null -eq $row -or [long]$row.pending -ne 1 -or [long]$row.earned -ne 1 -or [long]$row.consumed -ne 0 -or [long]$row.ordinal -ne 1) {
                throw "$role did not open its $field match with exactly one pending opening choice: $output"
            }
        }
        if ($reports[$role].opening.offer -eq $reports[$role].second_opening.offer) { throw "$role's second match reused its opening offer: $output" }
    }
    if (-not $reports['Client1'].opening.staged_preset) { throw "The buyer's opening offer is not the Host's staged preset: $output" }
    $hostReport = $reports['Host']
    $buyer = $reports['Client1']
    if (-not $hostReport.growth_fixture -or $buyer.growth_fixture -or -not $hostReport.buyer_killed) {
        throw "Only the Host may run the DEV growth fixture, and it must have killed the buyer: $output"
    }
    # The Listen Host as killer: its contender and camp rewards reached its own view and HUD (the probe's N03 checks).
    if ([long]$hostReport.host_killer.contender_gold -le 0 -or [long]$hostReport.host_killer.camp_kills -ne 2 -or
        [long]$hostReport.host_killer.camp_gold -le 0) {
        throw "The Host's own kill rewards are missing from its report: $output"
    }
    if (-not $buyer.replacement.track -or $buyer.replacement.track -ne $buyer.replacement.relearned_track -or
        [long]$buyer.replacement.ready_at_ms -ne [long]$buyer.replacement.relearned_ready_at_ms -or
        [long]$buyer.dead_choice.max_hp_p2_after -le [long]$buyer.dead_choice.max_hp_p2_before -or
        [long]$buyer.dead_choice.respawned_life -ne [long]$buyer.dead_choice.life + 1) {
        throw "The buyer's replacement or choice while dead is missing from its report: $output"
    }
    if ($buyer.match1.generation -ne $hostReport.match1.generation -or $buyer.second_generation -ne $hostReport.second_generation -or
        $buyer.generation -ne $buyer.second_generation -or $hostReport.generation -ne $hostReport.second_generation -or
        $buyer.second_generation -le $buyer.match1.generation) {
        throw "Roles did not observe the same first and second match generations: $output"
    }
    foreach ($name in @('LedgerEvidence.json','LedgerEvidence2.json','LedgerEvidence3.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $evidence $name))) { throw "The Host exported no $name`: $output" }
        # The independent replay of each exported ledger; a non-zero exit fails the run.
        Assert-LedgerEvidence $name
    }
    # First match: the authority's final state must equal the owner's own projection at the end of that match.
    $first = Read-GrowthJson 'LedgerEvidence.json'
    $current = $first.current
    # The only DevFixture mint of the run is the Host's own grant; the buyer's gold came from kills and trades.
    $grants = @($first.devFixture.grants)
    if (-not $first.devFixture.enabled -or $grants.Count -ne 1 -or $grants[0].account -ne $hostReport.match1.entity -or
        [long]$grants[0].gold -ne [long]$hostReport.host_grant_gold -or [long]$first.devFixture.minted -ne [long]$hostReport.host_grant_gold) {
        throw "The first match's ledger does not list exactly the Host's own DevFixture grant: $output"
    }
    $account = @($current.accounts | Where-Object { $_.id -eq $buyer.match1.entity })
    if ($account.Count -ne 1 -or [long]$account[0].gold -ne [long]$buyer.match1.gold) {
        throw "The first match's exported ledger and the owner projection disagree on the buyer's gold: $output"
    }
    $hostAccount = @($current.accounts | Where-Object { $_.id -eq $hostReport.match1.entity })
    if ($hostAccount.Count -ne 1 -or [long]$hostAccount[0].gold -ne [long]$hostReport.match1.gold) {
        throw "The first match's exported ledger and the Host's own projection disagree on the Host's gold: $output"
    }
    $ledgerItems = (Get-ItemKeys @($current.items | Where-Object { $_.owner -eq $buyer.match1.entity })) -join ','
    if ($ledgerItems -ne ((Get-ItemKeys $buyer.match1.items) -join ',')) {
        throw "The first match's exported ledger and the owner projection disagree on the buyer's items: $output"
    }
    # Second match: a fresh ledger under a new LedgerMatch with only the two contenders at the approved starting gold.
    $second = Read-GrowthJson 'LedgerEvidence2.json'
    $next = $second.current
    if ($second.match -eq $first.match) { throw "The second match reused the first match's LedgerMatch: $output" }
    if (-not $second.devFixture.enabled -or @($second.devFixture.grants).Count -ne 0 -or $second.devFixture.minted -ne '0') {
        throw "The second match's ledger carries DevFixture mints: $output"
    }
    if ($second.prefix.throughSerial -ne '0' -or @($second.tail).Count -ne 0 -or $next.throughSerial -ne '0' -or
        $next.issuedThrough -ne '0' -or $next.minted -ne '0' -or $next.burned -ne '0' -or @($next.items).Count -ne 0) {
        throw "The second match's ledger is not freshly initialized: $output"
    }
    $accountIds = (@($next.accounts | ForEach-Object { $_.id }) | Sort-Object) -join ','
    if ($accountIds -ne ((@($buyer.entity, $hostReport.entity) | Sort-Object) -join ',')) {
        throw "The second match's ledger accounts are not exactly this match's two contenders: $output"
    }
    if (@($next.accounts | Where-Object { $_.gold -ne $second.startingGold -or $_.revision -ne '1' -or $_.retired }).Count -ne 0 -or
        @($next.inventories | Where-Object { $_.revision -ne '1' }).Count -ne 0 -or [long]$buyer.match2_gold -ne [long]$second.startingGold) {
        throw "A second-match account does not open at the approved starting gold and first revisions: $output"
    }
    # DEV test commands (#78), after the clean second ledger: exactly the two owners' own DEV gold grants are DevFixture mints;
    # the buyer's DEV buy and sell are ordinary Buy/Sell entries, so its final gold and items equal the replayed ledger.
    $third = Read-GrowthJson 'LedgerEvidence3.json'
    if ($third.match -ne $second.match) { throw "The DEV-stage ledger is not the second match's: $output" }
    $devGrants = @($third.devFixture.grants)
    $buyerGrant = @($devGrants | Where-Object { $_.account -eq $buyer.entity })
    $hostGrant = @($devGrants | Where-Object { $_.account -eq $hostReport.entity })
    if (-not $third.devFixture.enabled -or $devGrants.Count -ne 2 -or $buyerGrant.Count -ne 1 -or $hostGrant.Count -ne 1 -or
        [long]$buyerGrant[0].gold -ne [long]$buyer.dev_grant_gold -or [long]$hostGrant[0].gold -ne [long]$hostReport.host_dev_gold -or
        [long]$buyer.dev_grant_gold -le 0 -or [long]$hostReport.host_dev_gold -le 0) {
        throw "The DEV-stage ledger does not list exactly each owner's own DEV gold grant: $output"
    }
    $buyerAccount = @($third.current.accounts | Where-Object { $_.id -eq $buyer.entity })
    if ($buyerAccount.Count -ne 1 -or [long]$buyerAccount[0].gold -ne [long]$buyer.final_gold -or
        @($third.current.items | Where-Object { $_.owner -eq $buyer.entity }).Count -ne 0 -or @($buyer.final_items).Count -ne 0) {
        throw "The DEV-stage ledger and the buyer's own projection disagree after its DEV buy and sell: $output"
    }
    $buyerResets = @($third.devFixture.resets | Where-Object { $_.account -eq $buyer.entity })
    if ($buyerResets.Count -ne 2 -or @($buyerResets | Where-Object { $_.instance }).Count -ne 1 -or
        @($third.devFixture.resets).Count -ne 2 -or [long]$buyer.final_gold -ne [long]$second.startingGold) {
        throw "The DEV-stage ledger does not list exactly the buyer's reset (one item, one gold change back to the start): $output"
    }
    if (@($buyer.dev_test).Count -ne 15 -or @($hostReport.dev_test).Count -ne 3) {
        throw "A DEV test step is missing from the reports: $output"
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
        throw "LD_DEV_FIXTURE_ACTIVE must be logged by the Host and never by the owning client: $output"
    }
    Write-Host ("DEV-only owned-connection growth/shop scenario passed: rendered participating Listen Host (victim, -LDDevGrowthFixture " +
        "stages the buyer's offers in the Lobby) plus one owning client; each owner opened each match with exactly one pending opening " +
        "choice, never re-granted by a respawn; level $($buyer.match1.level) from $($buyer.kills) kills, buy x2 / full / " +
        "replay / item active / sell with cooldown debt / outside at the shop; Q's cooling rank-2 skill replaced after a refused " +
        "unconfirmed pick, relearned at rank 1 on its old track and deadline; after the Host's own DevFixture grant it killed the buyer, " +
        "which chose while dead without revive or heal and respawned full; the Host's own kill and two low-camp last hits paid their " +
        "exact rewards into its own view and rendered HUD; first ledger replayed with --dev-fixture and equal to both owner projections. " +
        "The Host then ended the match " +
        "(no winner) and returned to Lobby generation $($buyer.second_generation): a second normal match started both owners at clean " +
        "growth, gold, equipment and cooldown state, refused the first match's choice, Buy and receipt query, quoted no inherited cooldown " +
        "and opened a fresh ledger under a new LedgerMatch (replayed). DEV test commands: the buyer through its remote owner RPC " +
        "(stale match, replay and conflict refused or answered without a second commit; gold grant, buy and sell away from the shop, " +
        "cooldown reset, refill, learn on the open opportunity, learn without one refused, a DEV AD term shown as its own row " +
        "and cleared, a reset to the match start booked as DevReset entries, a match-wide camp refresh refused) and the Host " +
        "through its own as Listen Host (gold grant, camp refresh, early capture opening); " +
        "the DEV-stage ledger replayed with --dev-fixture lists exactly both owners' grants. " +
        "Not camp lifecycle, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
