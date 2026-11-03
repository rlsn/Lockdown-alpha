[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(180,900)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoSessionFaults-Packaged' } else { 'DemoSessionFaults' })
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$projectRoot = Get-LDProjectRoot
$digest = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$version = Get-LDProjectVersion
# The wire version is read from the native source, so this runner cannot drift from the build it launches.
$entryHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'Source\Lockdown\Public\Framework\LDDemoEntry.h') -Raw
if ($entryHeader -notmatch 'inline\s+constexpr\s+uint32\s+WireProtocol\s*=\s*(\d+)\s*;') { throw 'Cannot read WireProtocol from LDDemoEntry.h' }
$wire = [int]$Matches[1]
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$errorPattern = 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED'
# Only the roles whose connection is meant to fail may log the engine's own failure report for it.
$expectedFailure = 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = (ConnectionLost|ConnectionTimeout|PendingConnectionFailure|FailureReceived),|LogNet: Error: TravelFailure: '
# Every role must report each check it owns; a skipped stage cannot pass silently.
$connection = 'Each role is a real Listen Host or owning client connection'
$required = @{
    'Leave/Host' = @($connection, 'Every provider is wired: the ready room may start normally', 'The match continues for the remaining contenders after a leave',
        'Leaving publishes no death notice', 'A Host-ended match records no winner', 'The result keeps the leaver as left, not dead',
        'Leaving credits no kill and marks no one else as left')
    'Leave/Client1' = @($connection, 'A Host-ended match records no winner', 'The result keeps the leaver as left, not dead',
        'Leaving credits no kill and marks no one else as left')
    'Leave/Client2Match' = @($connection, 'The in-match menu offers leave and its confirmation')
    'Leave/Client2' = @('A voluntary leave is not reported as a failure', 'The menu shows no failure or interruption after a voluntary leave')
    'HostLoss/Host' = @($connection, 'Every provider is wired: the ready room may start normally', 'The Host holds a live match with its owning client before it is lost')
    'HostLoss/Client1Match' = @($connection)
    'HostLoss/Client1' = @('The interruption follows the Host process being killed mid-match', 'Host loss never produced a result table',
        'A lost session is not reported as a failed join')
    'Join/Joiner' = @('The frontend offers the name, address, port and Join controls', 'A failed join shows its reason, not a generic ended notice',
        'A failed join names its reason once', 'The session reason is shown without a duplicated prefix', 'A failed join stays on the frontend',
        'The Join control is usable again after a failure')
    'Refuse/Host' = @($connection, 'Every provider is wired: the ready room may start normally', 'Refused joins never enter the room or its running match')
    'Refuse/Client1' = @($connection)
    'Refuse/Client2' = @($connection)
    'Refuse/Joiner' = @('The frontend offers the name, address, port and Join controls', 'A version mismatch is refused with its own reason',
        'A full room is refused with its own reason', 'A started match is refused with its own reason', 'A refused join stays on the frontend',
        'The Join control is usable again after a refusal')
    'Race/Host' = @($connection, 'Every provider is wired: the ready room may start normally', 'Refused joins never enter the room or its running match')
    'Race/Client1' = @($connection)
    'Race/Joiner' = @('The frontend offers the name, address, port and Join controls',
        'A join that loses the race to the match start is refused with its own reason', 'A refused join stays on the frontend',
        'The Join control is usable again after a refusal')
    'HostLeave/HostMatch' = @($connection, 'Every provider is wired: the ready room may start normally',
        'The Host holds a live match with its owning client before it leaves', 'The in-match menu offers leave and its confirmation')
    'HostLeave/Host' = @('A voluntary leave is not reported as a failure', 'The menu shows no failure or interruption after a voluntary leave')
    'HostLeave/Client1Match' = @($connection)
    'HostLeave/Client1' = @('The room closed because the Host left through its menu', 'The client shows that the Host left, not a lost connection',
        'A closed room never produced a result table', 'A closed room is not reported as a failed join')
}

function New-LoopbackPort {
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    $port = $socket.Client.LocalEndPoint.Port
    $socket.Dispose()
    $port
}

function Start-FaultRole {
    param([string]$Phase, [string]$Role, [string]$Url, [switch]$Ui, [int]$Port, [int]$ClosedPort, [int]$HostPort, [string]$ProductOverride)
    $user = Join-Path $output "$Phase-$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # Roles that read the frontend or press menu buttons need Slate to tick, so they render offscreen.
    $render = if ($Ui) { '-windowed -RenderOffScreen -ResX=960 -ResY=540' } else { '-nullrhi' }
    $portArgument = if ($Port) { "-port=$Port" } else { '' }
    $probeArguments = @(
        if ($ClosedPort) { "-LDDemoSessionFaultClosedPort=$ClosedPort" }
        if ($HostPort) { "-LDDemoSessionFaultHostPort=$HostPort" }
        if ($ProductOverride) { "-LDDemoSessionFaultProductOverride=$ProductOverride" }) -join ' '
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} {1} {2} -UserDir="{3}" -LDDemoSessionFaultProbe -LDDemoSessionFaultMode={4} -LDDemoSessionFaultRole={5} -LDDemoSessionFaultTimeout={6} -LDDemoSessionFaultOutput="{7}" -abslog="{8}"' -f $render,$portArgument,$probeArguments,$user,$Phase,$Role,$TimeoutSeconds,(Join-Path $launch.ProbeOutput $Phase),(Join-Path $output "$Phase-$Role.log")
    $process = Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru
    $processes.Add($process)
    $process
}

function Get-FaultFailure {
    param([string]$Directory)
    foreach ($file in @(Get-ChildItem -LiteralPath $Directory -Filter '*.json' -File -ErrorAction SilentlyContinue)) {
        $report = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        if ($report.PSObject.Properties['passed'] -and -not $report.passed) { return "$($file.Name): $($report.detail)" }
    }
    'no role report explains it'
}

function Wait-FaultFiles {
    param([string]$Directory, [string[]]$Names, [Diagnostics.Process[]]$Watch)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $Watch) {
            $process.Refresh()
            if ($process.HasExited) { throw "Session fault process exited early: PID $($process.Id) code $($process.ExitCode); $(Get-FaultFailure $Directory). Evidence: $output" }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Directory $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Session fault evidence timed out waiting for $($Names -join ', '): $(Get-FaultFailure $Directory). Evidence: $output"
}

function Read-FaultReport {
    param([string]$Phase, [string]$Name)
    $report = Get-Content -LiteralPath (Join-Path (Join-Path $launch.ProbeOutput $Phase) "$Name.json") -Raw | ConvertFrom-Json
    $failed = @($report.checks | Where-Object { -not $_.passed })
    if (-not $report.passed -or $failed.Count -ne 0) { throw "$Phase/$Name failed: $($report.detail) Evidence: $output" }
    foreach ($check in $required["$Phase/$Name"]) {
        if (@($report.checks | Where-Object { $_.name -eq $check }).Count -eq 0) { throw "$Phase/$Name lacks its check '$check'. Evidence: $output" }
    }
    $report
}

function Stop-Role {
    param([Diagnostics.Process]$Process, [string]$Name)
    # A fresh package's first rendered shutdown stores driver pipeline caches: usually about 40 s once, once over
    # 120 s (final 9167f8d suite, where the warm rerun passed), about 2 s warm.
    # The exit must still be clean.
    if (-not $Process.WaitForExit(300000) -or $Process.ExitCode -ne 0) { throw "$Name did not exit cleanly: $output" }
}

function Assert-FaultLogs {
    param([string]$Phase, [string[]]$FailingRoles)
    foreach ($log in @(Get-ChildItem -LiteralPath $output -Filter "$Phase-*.log" -File)) {
        $role = $log.BaseName.Substring($Phase.Length + 1)
        $lines = @(Select-String -LiteralPath $log.FullName -Pattern $errorPattern | Where-Object { -not ($FailingRoles -contains $role -and $_.Line -match $expectedFailure) })
        if ($lines.Count) { throw "Session fault scenario logged an unexpected error in $($log.Name): $($lines[0].Line). Evidence: $output" }
    }
}

$clientUrl = { param([int]$Port, [string]$Name) '127.0.0.1:{0}?Name={1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $Port,$Name,$wire,$version,$digest }
try {
    # Phase 1: a contender leaves mid-match through the real menu; the room continues and the leave is no kill.
    $directory = Join-Path $launch.ProbeOutput 'Leave'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $port = New-LoopbackPort
    $leaveHost = Start-FaultRole -Phase 'Leave' -Role 'Host' -Url "/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=FaultHost" -Port $port
    Wait-FaultFiles $directory @('HostJoined.json') @($leaveHost)
    $stayer = Start-FaultRole -Phase 'Leave' -Role 'Client1' -Url (& $clientUrl $port 'FaultStayer')
    $leaver = Start-FaultRole -Phase 'Leave' -Role 'Client2' -Url (& $clientUrl $port 'FaultLeaver') -Ui
    Wait-FaultFiles $directory @('Host.json', 'Client1.json', 'Client2.json', 'Client2Match.json') @($leaveHost, $stayer, $leaver)
    $leaveHostReport = Read-FaultReport 'Leave' 'Host'
    $stayerReport = Read-FaultReport 'Leave' 'Client1'
    $null = Read-FaultReport 'Leave' 'Client2Match'
    $null = Read-FaultReport 'Leave' 'Client2'
    if ($leaveHostReport.summary.result_id -ne $stayerReport.summary.result_id) { throw "Host and the remaining client disagree on the Host-ended result: $output" }
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Disconnect owning clients'
    Stop-Role $stayer 'Remaining client'
    Stop-Role $leaver 'Leaving client'
    Set-Content -LiteralPath (Join-Path $directory 'Finish') -Value 'Complete'
    Stop-Role $leaveHost 'Leave Host'
    Assert-FaultLogs 'Leave' @()

    # Phase 2: the Host process dies mid-match; the client must land on an interrupted menu, never a result.
    $directory = Join-Path $launch.ProbeOutput 'HostLoss'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $port = New-LoopbackPort
    $lostHost = Start-FaultRole -Phase 'HostLoss' -Role 'Host' -Url "/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=LostHost" -Port $port
    Wait-FaultFiles $directory @('HostJoined.json') @($lostHost)
    $orphan = Start-FaultRole -Phase 'HostLoss' -Role 'Client1' -Url (& $clientUrl $port 'FaultOrphan') -Ui
    Wait-FaultFiles $directory @('Host.json', 'Client1InMatch.json', 'Client1Match.json') @($lostHost, $orphan)
    $null = Read-FaultReport 'HostLoss' 'Host'
    $null = Read-FaultReport 'HostLoss' 'Client1Match'
    Set-Content -LiteralPath (Join-Path $directory 'HostKilled') -Value 'The runner killed the Host process mid-match'
    Stop-Process -Id $lostHost.Id -Force
    if (-not $lostHost.WaitForExit(30000)) { throw "The Host process could not be killed: $output" }
    Wait-FaultFiles $directory @('Client1.json') @($orphan)
    $orphanReport = Read-FaultReport 'HostLoss' 'Client1'
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Disconnect owning clients'
    Stop-Role $orphan 'Client after Host loss'
    Assert-FaultLogs 'HostLoss' @('Client1')

    # Phase 3: real Join presses against a closed loopback port and an unresolvable host name.
    $directory = Join-Path $launch.ProbeOutput 'Join'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $joiner = Start-FaultRole -Phase 'Join' -Role 'Joiner' -Ui -ClosedPort (New-LoopbackPort)
    Wait-FaultFiles $directory @('Joiner.json') @($joiner)
    $joinReport = Read-FaultReport 'Join' 'Joiner'
    if (-not $joinReport.summary.closed_port_notice -or -not $joinReport.summary.unresolvable_host_notice) { throw "The Join phase did not record both failure notices: $output" }
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Exit'
    Stop-Role $joiner 'Joining client'
    Assert-FaultLogs 'Join' @('Joiner')

    # Phase 4: a live Host refuses real Join presses, each with its own coded reason: another product version, a full
    # room of three humans, then a started match. The joiner announces another version, so any attempt that passed the
    # room checks would still be refused, never admitted.
    $directory = Join-Path $launch.ProbeOutput 'Refuse'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $port = New-LoopbackPort
    $otherVersion = if ($version -ne '0.0.1') { '0.0.1' } else { '0.0.2' }
    $refuseHost = Start-FaultRole -Phase 'Refuse' -Role 'Host' -Url "/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=RefuseHost" -Port $port
    Wait-FaultFiles $directory @('HostJoined.json') @($refuseHost)
    $refused = Start-FaultRole -Phase 'Refuse' -Role 'Joiner' -Ui -HostPort $port -ProductOverride $otherVersion
    Wait-FaultFiles $directory @('JoinerMismatch.json') @($refuseHost, $refused)
    $filler1 = Start-FaultRole -Phase 'Refuse' -Role 'Client1' -Url (& $clientUrl $port 'FaultFiller1')
    $filler2 = Start-FaultRole -Phase 'Refuse' -Role 'Client2' -Url (& $clientUrl $port 'FaultFiller2')
    Wait-FaultFiles $directory @('Host.json', 'Client1.json', 'Client2.json', 'Joiner.json') @($refuseHost, $refused, $filler1, $filler2)
    $null = Read-FaultReport 'Refuse' 'Host'
    $null = Read-FaultReport 'Refuse' 'Client1'
    $null = Read-FaultReport 'Refuse' 'Client2'
    $refuseReport = Read-FaultReport 'Refuse' 'Joiner'
    foreach ($field in 'version_mismatch_notice', 'room_full_notice', 'match_started_notice') {
        if (-not $refuseReport.summary.$field) { throw "The Refuse phase did not record its $($field): $output" }
    }
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Disconnect owning clients'
    Stop-Role $filler1 'First filling client'
    Stop-Role $filler2 'Second filling client'
    Stop-Role $refused 'Refused client'
    Set-Content -LiteralPath (Join-Path $directory 'Finish') -Value 'Complete'
    Stop-Role $refuseHost 'Refusing Host'
    Assert-FaultLogs 'Refuse' @('Joiner')

    # Phase 5: a real Join passes PreLogin, then the Host starts while that client loads the room. The final admission
    # check refuses it, and the client shows the started-match reason rather than a lost session.
    $directory = Join-Path $launch.ProbeOutput 'Race'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $port = New-LoopbackPort
    $raceHost = Start-FaultRole -Phase 'Race' -Role 'Host' -Url "/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=RaceHost" -Port $port
    Wait-FaultFiles $directory @('HostJoined.json') @($raceHost)
    $raceFiller = Start-FaultRole -Phase 'Race' -Role 'Client1' -Url (& $clientUrl $port 'FaultFiller')
    $racer = Start-FaultRole -Phase 'Race' -Role 'Joiner' -Ui -HostPort $port
    Wait-FaultFiles $directory @('Host.json', 'Client1.json', 'Joiner.json') @($raceHost, $raceFiller, $racer)
    $raceHostReport = Read-FaultReport 'Race' 'Host'
    $null = Read-FaultReport 'Race' 'Client1'
    $raceReport = Read-FaultReport 'Race' 'Joiner'
    if (-not $raceHostReport.summary.started_while_join_loads) { throw "The Race Host did not start while the join was loading the room: $output" }
    if (-not (Select-String -LiteralPath (Join-Path $output 'Race-Host.log') -Pattern 'LD_DEMO_ADMISSION_REJECTED MatchAlreadyStarted:' -SimpleMatch -Quiet)) {
        throw "The Race refusal did not come from the Host's final admission check: $output"
    }
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Disconnect owning clients'
    Stop-Role $raceFiller 'Race filling client'
    Stop-Role $racer 'Racing client'
    Set-Content -LiteralPath (Join-Path $directory 'Finish') -Value 'Complete'
    Stop-Role $raceHost 'Race Host'
    Assert-FaultLogs 'Race' @('Joiner')

    # Phase 6: the Listen Host leaves mid-match through its real menu. The room closes for its client when the Host's
    # map change begins, before the engine shuts the connection down, so the client shows that the Host left.
    $directory = Join-Path $launch.ProbeOutput 'HostLeave'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $port = New-LoopbackPort
    $leavingHost = Start-FaultRole -Phase 'HostLeave' -Role 'Host' -Url "/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=LeavingHost" -Port $port -Ui
    Wait-FaultFiles $directory @('HostJoined.json') @($leavingHost)
    $remaining = Start-FaultRole -Phase 'HostLeave' -Role 'Client1' -Url (& $clientUrl $port 'FaultRemaining') -Ui
    Wait-FaultFiles $directory @('HostMatch.json', 'Host.json', 'Client1Match.json', 'Client1.json') @($leavingHost, $remaining)
    $null = Read-FaultReport 'HostLeave' 'HostMatch'
    $null = Read-FaultReport 'HostLeave' 'Host'
    $null = Read-FaultReport 'HostLeave' 'Client1Match'
    $hostLeftReport = Read-FaultReport 'HostLeave' 'Client1'
    if (-not (Select-String -LiteralPath (Join-Path $output 'HostLeave-Host.log') -Pattern 'LD_DEMO_ROOM_CLOSED_BY_HOST' -SimpleMatch -Quiet)) {
        throw "The Host's room did not close before its map change: $output"
    }
    Set-Content -LiteralPath (Join-Path $directory 'FinishClients') -Value 'Exit'
    Stop-Role $remaining 'Client after the Host left'
    Set-Content -LiteralPath (Join-Path $directory 'Finish') -Value 'Complete'
    Stop-Role $leavingHost 'Leaving Host'
    Assert-FaultLogs 'HostLeave' @('Client1')

    $kind = if ($launch.Packaged) { "packaged build $($launch.Package)" } else { 'editor game target' }
    Write-Host ("DEV-only session faults passed on the ${kind}: mid-match menu leave (match continued, no death notice, Host-ended result marks the leaver left with no kill credited); " +
        "Host killed mid-match -> client menu shows '$($orphanReport.summary.notice)'; failed joins -> '$($joinReport.summary.closed_port_notice)' and '$($joinReport.summary.unresolvable_host_notice)'; " +
        "refused joins -> '$($refuseReport.summary.version_mismatch_notice)', '$($refuseReport.summary.room_full_notice)' and '$($refuseReport.summary.match_started_notice)'; " +
        "a join that lost the race to the start -> '$($raceReport.summary.race_notice)'; " +
        "Host left through its menu -> client menu shows '$($hostLeftReport.summary.notice)'. " +
        "All on one machine's loopback: not LAN or public internet. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
