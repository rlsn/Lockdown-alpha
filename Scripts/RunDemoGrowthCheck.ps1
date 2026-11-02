[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(300,1500)][int]$TimeoutSeconds=900)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'DemoGrowth-OwnedShop'
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
$project = Join-Path $projectRoot 'Lockdown.uproject'
$binary = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$roles = @('Host','Client1')
# Every role must report the check groups it owns (PROGRESSION_EQUIPMENT_ECONOMY §12 ids); a skipped stage cannot pass silently.
$requiredChecks = @{
    Host = @('Net','Start','Ledger')
    Client1 = @('Net','Start','N03','A01','A06','A10','A15','A16','A17','A24')
}

function Start-GrowthRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view; the owning client needs no renderer.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port" } else { '-nullrhi' }
    $arguments = '"{0}" "{1}" -game -unattended -nop4 -nosound -multihome=127.0.0.1 {2} -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{3}" -LDDemoGrowthProbe -LDDemoGrowthRole={4} -LDDemoGrowthTimeout={5} -LDDemoGrowthOutput="{6}" -abslog="{7}"' -f $project,$Url,$render,$user,$Role,$TimeoutSeconds,$output,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru))
}

function Wait-GrowthFiles {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $output 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Growth scenario process exited before its evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $output $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Growth scenario evidence timed out: $output"
}

function Read-GrowthJson {
    param([string]$Name)
    Get-Content -LiteralPath (Join-Path $output $Name) -Raw | ConvertFrom-Json
}

# The independent replay of the exported ledger; a non-zero exit fails the run.
function Assert-LedgerEvidence {
    param([string]$Name)
    Invoke-LDTool -Tool 'python' -ToolArguments @('-B', $checker, (Join-Path $output $Name)) -LogPath (Join-Path $output "$Name.check.txt")
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
    foreach ($role in $roles) {
        $report = Read-GrowthJson "$role.json"
        $failed = @($report.checks | Where-Object { -not $_.passed })
        if (-not $report.passed -or $report.role -ne $role -or -not $report.dev_probe -or
            $report.catalog_sha256 -ne $digest -or $failed.Count -ne 0) {
            throw "Owned growth evidence failed for $role`: $($report.detail) Evidence: $output"
        }
        foreach ($id in $requiredChecks[$role]) {
            if (@($report.checks | Where-Object { $_.id -eq $id }).Count -eq 0) { throw "$role report lacks its $id checks: $output" }
        }
        $reports[$role] = $report
    }
    $hostReport = $reports['Host']
    $buyer = $reports['Client1']
    if ($buyer.generation -ne $hostReport.generation) { throw "Roles observed different match generations: $output" }
    if (-not (Test-Path -LiteralPath (Join-Path $output 'LedgerEvidence.json'))) { throw "The Host exported no ledger evidence: $output" }
    # The independent replay must pass, and the authority's final state must equal the owner's own projection.
    Assert-LedgerEvidence 'LedgerEvidence.json'
    $current = (Read-GrowthJson 'LedgerEvidence.json').current
    $account = @($current.accounts | Where-Object { $_.id -eq $buyer.entity })
    if ($account.Count -ne 1 -or [long]$account[0].gold -ne [long]$buyer.final_gold) {
        throw "The exported ledger and the owner projection disagree on the buyer's gold: $output"
    }
    $ledgerItems = (Get-ItemKeys @($current.items | Where-Object { $_.owner -eq $buyer.entity })) -join ','
    if ($ledgerItems -ne ((Get-ItemKeys $buyer.final_items) -join ',')) {
        throw "The exported ledger and the owner projection disagree on the buyer's items: $output"
    }
    Set-Content -LiteralPath (Join-Path $output 'FinishClients') -Value 'Disconnect owning clients'
    if (-not $processes[1].WaitForExit(30000) -or $processes[1].ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    Set-Content -LiteralPath (Join-Path $output 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host ("DEV-only owned-connection growth/shop scenario passed: rendered participating Listen Host (victim) plus one owning client; " +
        "$($buyer.kills) kills, one choice, buy x2 / full / replay / sell / outside at the shop; ledger evidence replayed and equal to " +
        "the owner projection. Normal Lobby start with every provider registered. " +
        "Not camps, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
}
