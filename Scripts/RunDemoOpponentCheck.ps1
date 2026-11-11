[CmdletBinding()]
# The default bound is derived in LDDemoOpponentProbe.cpp (StartOpponentProbe): the flagpole opens at most 1464 s after the
# start when every rest runs its whole approved PK, plus the 505 s kept for the lobby, the capture contest and the results.
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,2400)][int]$TimeoutSeconds=1980,
    # Optional engine packet simulation for every role, e.g. '-PktLag=40 -PktLagVariance=10 -PktLoss=1'.
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation,
    # Optional log verbosity for every role, e.g. 'LogLDNav Verbose' or 'LogLDTrace off, LogLDCombat Display' (passed as -LogCmds).
    [ValidatePattern('^[A-Za-z0-9_]+ [A-Za-z]+(, ?[A-Za-z0-9_]+ [A-Za-z]+)*$')][string]$LogCmds,
    # DEV telemetry instead of the full-room check: one idle headless Listen Host and three AI play -MatchCount matches in a
    # row; each match gets -TimeoutSeconds. Writes one MatchNN.json per match and Summary.json; it judges nothing.
    [switch]$Telemetry, [ValidateRange(1,50)][int]$MatchCount=1,
    # Telemetry only: DEV content hot reload (-LDDevContentReload, editor build only), so a re-synced demo_content.json
    # is played without rebuilding C++. Each match records the content root it played.
    [switch]$DevContentReload,
    # Presentation-only R0 check on real owning connections, not a full AI match. Pins only the DEV draw seed.
    [ValidateSet('2v2','3v3')][string]$UIReview)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
if ($Telemetry -and $NetEmulation) { throw 'Telemetry runs the Host alone on loopback; -NetEmulation has no connection to impair.' }
if (-not $Telemetry -and $PSBoundParameters.ContainsKey('MatchCount')) { throw '-MatchCount applies only to -Telemetry.' }
if ($DevContentReload -and (-not $Telemetry -or $PackageRoot)) { throw '-DevContentReload applies only to -Telemetry on the editor build; a package must be cooked again.' }
if ($UIReview -and $Telemetry) { throw '-UIReview and -Telemetry are separate checks.' }
$scenario = if ($UIReview) { "DemoUI-$UIReview" } elseif ($Telemetry) { 'DemoOpponent-Telemetry' } else { 'DemoOpponent-FullRoom' }
$output = New-LDArtifactDirectory ($scenario + $(if ($PackageRoot) { '-Packaged' } else { '' }) +
    $(if ($NetEmulation) { '-NetEmulated' } else { '' }))
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
    # The Host renders (1280x720 offscreen) so it can keep HUD screenshots of the match; clients and the telemetry
    # Host stay headless.
    $portArgument = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port" }
        elseif ($Role -eq 'Telemetry') { "-nullrhi -port=$port -LDDemoOpponentMatches=$MatchCount$(if ($DevContentReload) { ' -LDDevContentReload' })" }
        elseif ($UIReview -and $Role -eq 'Client1') { '-windowed -RenderOffScreen -ResX=1280 -ResY=720' }
        else { '-nullrhi' }
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoOpponentProbe -LDDemoOpponentRole={2} -LDDemoOpponentTimeout={3} -LDDemoOpponentOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    if ($NetEmulation) { $arguments += " $NetEmulation" }
    if ($LogCmds) { $arguments += " -LogCmds=""$LogCmds""" }
    if ($UIReview) { $arguments += " -LDDemoUIReview=$UIReview" }
    if ($UIReview -and $Role -eq 'Host') { $arguments += ' -LDDevGrowthFixture' }
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

# Clients can connect as soon as the Host's lobby exists; its own HostJoined marker comes many seconds later, once the
# rendered Host's probe first ticks, and the probe still waits for every role's marker before the lobby steps.
function Wait-HostLobby {
    $log = Join-Path $output 'Host.log'
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while ([DateTime]::UtcNow -lt $deadline) {
        $processes[0].Refresh()
        if ($processes[0].HasExited) { throw "Opponent scenario Host exited before its lobby opened: $(Get-OpponentFailure). Evidence: $output" }
        if ((Test-Path -LiteralPath (Join-Path $evidence 'HostJoined.json')) -or
            ((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern 'LD_DEMO_LOBBY generation=' -SimpleMatch -Quiet))) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Opponent scenario Host lobby timed out: $output"
}

function Invoke-OpponentTelemetry {
    Start-OpponentRole 'Telemetry' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoTelemetryHost'
    # The Host ends itself after the last match, or on the first failure with its reason in Telemetry.json.
    $process = $processes[0]
    if (-not $process.WaitForExit((($TimeoutSeconds * $MatchCount) + 60) * 1000)) { throw "Opponent telemetry timed out: $output" }
    $file = Join-Path $evidence 'Telemetry.json'
    if (-not (Test-Path -LiteralPath $file)) { throw "Opponent telemetry left no report (exit $($process.ExitCode)): $output" }
    $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    if (-not $report.passed -or $process.ExitCode -ne 0) {
        throw "Opponent telemetry failed after $($report.matches_completed) of $MatchCount match(es): $($report.detail). Evidence: $output"
    }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Opponent telemetry logged an error: $($errors[0].Line). Evidence: $output" }
    Invoke-LDTool -Tool 'python' -ToolArguments @('-B', (Join-Path $PSScriptRoot 'SummarizeOpponentTelemetry.py'), $evidence) -LogPath (Join-Path $output 'Summary.txt')
    Write-Host "DEV-only opponent telemetry recorded $MatchCount match(es) of one idle Host and three AI. It describes these matches; it is not an AI acceptance check or a balance verdict. Evidence: $output"
}

try {
    if ($Telemetry) { Invoke-OpponentTelemetry; return }
    Start-OpponentRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoOpponentHost'
    Wait-HostLobby
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
    foreach ($r in $reports | Where-Object { -not $UIReview }) {
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
    $loggedErrors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    # R02 intentionally blocks every PK spawn. Only its single Host marker, backed by the passed assertion, is expected.
    $r02Passed = @($reports | Where-Object { $_.role -eq 'Host' } | ForEach-Object { $_.checks } |
        Where-Object { $_.name -like 'R02:*' -and $_.status -eq 'passed' }).Count -eq 1
    $expectedBlocked = @($loggedErrors | Where-Object {
        $_.Filename -eq 'Host.log' -and $_.Line -match 'LogLDMatch: Error: LD_PK_ARENA_BLOCKED serial=2 outcome=NoContest at_ms=\d+$'
    })
    $errors = @($loggedErrors | Where-Object {
        -not ($r02Passed -and $expectedBlocked.Count -eq 1 -and $_.Path -eq $expectedBlocked[0].Path -and $_.LineNumber -eq $expectedBlocked[0].LineNumber)
    })
    if ($errors.Count) { throw "Opponent scenario logged an error: $($errors[0].Line). Evidence: $output" }
    Assert-LDNetEmulationApplied -Output $output -Roles @('Host','Client1','Client2') -NetEmulation $NetEmulation
    if ($UIReview) {
        foreach ($role in 'Host','Client1','Client2') {
            $r = $reports | Where-Object { $_.role -eq $role }
            if ($r.summary.ui_format -ne $UIReview -or @($r.checks | Where-Object { $_.name -eq 'UI: owning connection reads team relations and observed arena bodies' -and $_.status -eq 'passed' }).Count -ne 1) {
                throw "UI review did not prove $UIReview for $role. Evidence: $output"
            }
            foreach ($scale in @('100','125') | Where-Object { $role -ne 'Client2' }) {
                $shot = Join-Path $evidence "$role-PK-$UIReview-$scale.png"
                if (-not (Test-Path -LiteralPath $shot)) { throw "UI review screenshot missing: $shot" }
            }
        }
        Write-Host "DEV-only $UIReview UI evidence passed on three real owning connections with the approved probability and a pinned draw seed. Not full-match AI acceptance. Evidence: $output"
        return
    }
    foreach ($ai in $first.opponents) {
        Write-Host ('AI slot {0}: level {1}, choices {2}, deaths {3}, respawns {4}, kills {5}, orders {6} (refused {7}, refused at admission {11}), actions {8}, max 30 s body requests {9}{10}' -f
            $ai.slot,$ai.level,$ai.choices,$ai.deaths,$ai.respawns,$ai.kills,$ai.orders,$ai.orders_refused,
            $ai.actions,$ai.max_body_requests_30s,$(if ($ai.winner) { ', winner' } else { '' }),$ai.orders_rejected)
    }
    $notExercised = @($reports | ForEach-Object { @($_.checks) } | Where-Object { $_.status -eq 'not_exercised' } |
        ForEach-Object { [string]$_.name } | Sort-Object -Unique)
    foreach ($name in $notExercised) { Write-Host "NOT EXERCISED (not counted as passed): $name" }
    $coverage = if ($notExercised.Count) { " $($notExercised.Count) check(s) were not exercised by this match; they are listed above and are not passes." } else { '' }
    $impairment = if ($NetEmulation) { " Every role ran under engine packet simulation ($NetEmulation) on one machine's loopback." } else { '' }
    Write-Host "DEV-only full-room opponent AI scenario passed on real owned connections: the humans stayed out of play and an AI won by capture after $([math]::Round($first.duration_ms / 1000)) s.$coverage$impairment Not hidden-state non-interference, a leaving human or network faults. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
