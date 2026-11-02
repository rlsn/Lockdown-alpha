[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,900)][int]$TimeoutSeconds=720)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoOpponent-FullRoom-Packaged' } else { 'DemoOpponent-FullRoom' })
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
# Probe markers and reports live where the probe accepts them; logs and user dirs stay beside the runner.
$evidence = $launch.ProbeOutput
$socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
$port = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$digest = (Get-FileHash -LiteralPath (Join-Path (Get-LDProjectRoot) 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$version = Get-LDProjectVersion
# The wire version is read from the native source, so this runner cannot drift from the build it launches.
$entryHeader = Get-Content -LiteralPath (Join-Path (Get-LDProjectRoot) 'Source\Lockdown\Public\Framework\LDDemoEntry.h') -Raw
if ($entryHeader -notmatch 'inline\s+constexpr\s+uint32\s+WireProtocol\s*=\s*(\d+)\s*;') { throw 'Cannot read WireProtocol from LDDemoEntry.h' }
$wire = [int]$Matches[1]
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()

function Start-OpponentRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoOpponentProbe -LDDemoOpponentRole={2} -LDDemoOpponentTimeout={3} -LDDemoOpponentOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Get-OpponentFailure {
    foreach ($role in 'Host','Client1','Client2') {
        $file = Join-Path $evidence "$role.json"
        if (Test-Path -LiteralPath $file) {
            $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (-not $report.passed) { return "$role reported: $($report.detail)" }
        }
    }
    return 'no role report explains it'
}

function Wait-OpponentReports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Opponent scenario process exited before evidence completed: PID $($process.Id); $(Get-OpponentFailure). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Opponent scenario evidence timed out: $output"
}

try {
    Start-OpponentRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoOpponentHost'
    Wait-OpponentReports @('HostJoined.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoOpponentClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-OpponentRole "Client$index" $url
    }
    Wait-OpponentReports @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $r = Get-Content -LiteralPath (Join-Path $evidence "$_.json") -Raw | ConvertFrom-Json
        # A check this match gave no occasion for is listed below as not exercised; it is neither a pass nor a failure.
        if (-not $r.passed -or $r.role -ne $_ -or @($r.checks | Where-Object { $_.status -ne 'passed' -and $_.status -ne 'not_exercised' }).Count -ne 0) {
            throw "Opponent scenario evidence failed for $_`: $($r.detail). Evidence: $output"
        }
        $r
    }
    $first = $reports[0].summary
    foreach ($r in $reports) {
        if ($r.summary.winner -ne $first.winner -or $r.summary.ended_at_ms -ne $first.ended_at_ms -or
            $r.summary.next_generation -ne $first.next_generation) {
            throw "Roles disagree on the unique result or the next Lobby generation: $output"
        }
    }
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Opponent scenario client did not exit cleanly: $output" }
    }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Opponent scenario logged an error: $($errors[0].Line). Evidence: $output" }
    foreach ($ai in $first.opponents) {
        Write-Host ('AI slot {0}: level {1}, choices {2}, items {3} [{4}], deaths {5}, respawns {6}, kills {7}, orders {8} (refused {9}), actions {10}, trades {11}, max 30 s body requests {12}{13}' -f
            $ai.slot,$ai.level,$ai.choices,$ai.items_max,$ai.gear,$ai.deaths,$ai.respawns,$ai.kills,$ai.orders,$ai.orders_refused,
            $ai.actions,$ai.trades,$ai.max_body_requests_30s,$(if ($ai.winner) { ', winner' } else { '' }))
    }
    $notExercised = @($reports | ForEach-Object { @($_.checks) } | Where-Object { $_.status -eq 'not_exercised' } |
        ForEach-Object { [string]$_.name } | Sort-Object -Unique)
    foreach ($name in $notExercised) { Write-Host "NOT EXERCISED (not counted as passed): $name" }
    $coverage = if ($notExercised.Count) { " $($notExercised.Count) check(s) were not exercised by this match; they are listed above and are not passes." } else { '' }
    Write-Host "DEV-only full-room opponent AI scenario passed on real owned connections: the humans stayed out of play and an AI won by capture after $([math]::Round($first.duration_ms / 1000)) s.$coverage Not hidden-state non-interference, a leaving human or network faults. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
