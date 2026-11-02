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
}

function New-LoopbackPort {
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    $port = $socket.Client.LocalEndPoint.Port
    $socket.Dispose()
    $port
}

function Start-FaultRole {
    param([string]$Phase, [string]$Role, [string]$Url, [switch]$Ui, [int]$Port, [int]$ClosedPort)
    $user = Join-Path $output "$Phase-$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # Roles that read the frontend or press menu buttons need Slate to tick, so they render offscreen.
    $render = if ($Ui) { '-windowed -RenderOffScreen -ResX=960 -ResY=540' } else { '-nullrhi' }
    $portArgument = if ($Port) { "-port=$Port" } else { '' }
    $closedArgument = if ($ClosedPort) { "-LDDemoSessionFaultClosedPort=$ClosedPort" } else { '' }
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} {1} {2} -UserDir="{3}" -LDDemoSessionFaultProbe -LDDemoSessionFaultMode={4} -LDDemoSessionFaultRole={5} -LDDemoSessionFaultTimeout={6} -LDDemoSessionFaultOutput="{7}" -abslog="{8}"' -f $render,$portArgument,$closedArgument,$user,$Phase,$Role,$TimeoutSeconds,(Join-Path $launch.ProbeOutput $Phase),(Join-Path $output "$Phase-$Role.log")
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
    # A fresh package's first rendered shutdown stores driver pipeline caches: about 40 s once on this machine,
    # about 2 s warm. The exit must still be clean.
    if (-not $Process.WaitForExit(120000) -or $Process.ExitCode -ne 0) { throw "$Name did not exit cleanly: $output" }
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

    $kind = if ($launch.Packaged) { "packaged build $($launch.Package)" } else { 'editor game target' }
    Write-Host ("DEV-only session faults passed on the ${kind}: mid-match menu leave (match continued, no death notice, Host-ended result marks the leaver left with no kill credited); " +
        "Host killed mid-match -> client menu shows '$($orphanReport.summary.notice)'; failed joins -> '$($joinReport.summary.closed_port_notice)' and '$($joinReport.summary.unresolvable_host_notice)'. " +
        "All on one machine's loopback: not LAN or public internet. Evidence: $output")
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
