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
    Host = @('Net','Start','Ledger','Iso')
    Client1 = @('Net','Start','N03','A01','A06','A10','A15','A16','A17','A19','A21','A24','Iso')
}

function Start-GrowthRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view; the owning client needs no renderer.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port" } else { '-nullrhi' }
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

# The independent replay of the exported ledger; a non-zero exit fails the run.
function Assert-LedgerEvidence {
    param([string]$Name)
    Invoke-LDTool -Tool 'python' -ToolArguments @('-B', $checker, (Join-Path $evidence $Name)) -LogPath (Join-Path $output "$Name.check.txt")
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
    $hostReport = $reports['Host']
    $buyer = $reports['Client1']
    if ($buyer.match1.generation -ne $hostReport.match1.generation -or $buyer.second_generation -ne $hostReport.second_generation -or
        $buyer.generation -ne $buyer.second_generation -or $hostReport.generation -ne $hostReport.second_generation -or
        $buyer.second_generation -le $buyer.match1.generation) {
        throw "Roles did not observe the same first and second match generations: $output"
    }
    foreach ($name in @('LedgerEvidence.json','LedgerEvidence2.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $evidence $name))) { throw "The Host exported no $name`: $output" }
        # The independent replay of each exported ledger; a non-zero exit fails the run.
        Assert-LedgerEvidence $name
    }
    # First match: the authority's final state must equal the owner's own projection at the end of that match.
    $first = Read-GrowthJson 'LedgerEvidence.json'
    $current = $first.current
    $account = @($current.accounts | Where-Object { $_.id -eq $buyer.match1.entity })
    if ($account.Count -ne 1 -or [long]$account[0].gold -ne [long]$buyer.match1.gold) {
        throw "The first match's exported ledger and the owner projection disagree on the buyer's gold: $output"
    }
    $ledgerItems = (Get-ItemKeys @($current.items | Where-Object { $_.owner -eq $buyer.match1.entity })) -join ','
    if ($ledgerItems -ne ((Get-ItemKeys $buyer.match1.items) -join ',')) {
        throw "The first match's exported ledger and the owner projection disagree on the buyer's items: $output"
    }
    # Second match: a fresh ledger under a new LedgerMatch with only the two contenders at the approved starting gold.
    $second = Read-GrowthJson 'LedgerEvidence2.json'
    $next = $second.current
    if ($second.match -eq $first.match) { throw "The second match reused the first match's LedgerMatch: $output" }
    if ($second.prefix.throughSerial -ne '0' -or @($second.tail).Count -ne 0 -or $next.throughSerial -ne '0' -or
        $next.issuedThrough -ne '0' -or $next.minted -ne '0' -or $next.burned -ne '0' -or @($next.items).Count -ne 0) {
        throw "The second match's ledger is not freshly initialized: $output"
    }
    $accountIds = (@($next.accounts | ForEach-Object { $_.id }) | Sort-Object) -join ','
    if ($accountIds -ne ((@($buyer.entity, $hostReport.entity) | Sort-Object) -join ',')) {
        throw "The second match's ledger accounts are not exactly this match's two contenders: $output"
    }
    if (@($next.accounts | Where-Object { $_.gold -ne $second.startingGold -or $_.revision -ne '1' -or $_.retired }).Count -ne 0 -or
        @($next.inventories | Where-Object { $_.revision -ne '1' }).Count -ne 0 -or [long]$buyer.final_gold -ne [long]$second.startingGold) {
        throw "A second-match account does not open at the approved starting gold and first revisions: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    if (-not $processes[1].WaitForExit(30000) -or $processes[1].ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host ("DEV-only owned-connection growth/shop scenario passed: rendered participating Listen Host (victim) plus one owning client; " +
        "level $($buyer.match1.level) from $($buyer.kills) kills, one choice, buy x2 / full / replay / item active / sell with " +
        "cooldown debt / outside at the shop; first ledger replayed and equal to the owner projection. The Host then ended the match " +
        "(no winner) and returned to Lobby generation $($buyer.second_generation): a second normal match started both owners at clean " +
        "growth, gold, equipment and cooldown state, refused the first match's choice, Buy and receipt query, quoted no inherited cooldown " +
        "and opened a fresh ledger under a new LedgerMatch (replayed). Not camps, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
