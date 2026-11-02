[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,1500)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoCombat-OwnedOrders-Packaged' } else { 'DemoCombat-OwnedOrders' })
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
    Host = @('Net','Start','AttackMove','Stop','Death','Respawn','Protection')
    Client1 = @('Net','Start','Settings','AttackMove','Stop','Death','DeathCamera','Respawn','Protection')
}

function Start-CombatRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCombatProbe -LDDemoCombatRole={2} -LDDemoCombatTimeout={3} -LDDemoCombatOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
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
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    if (-not $processes[1].WaitForExit(30000) -or $processes[1].ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host ("DEV-only owned-connection combat/orders scenario passed through the ordinary lobby Start: Settings held " +
        "over an accepted walk past its lease (order and heartbeat continue, nothing replays), remote attack-move " +
        "and S, right-click kill with kill-feed credit, a minimap pan of the static death camera, respawn at spawn anchor " +
        "$($hostReport.respawn_anchor) with exact protection, protection spent by an empty Q. " +
        "Not skills, gear actives, camps, AI, capture or full-match acceptance. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
