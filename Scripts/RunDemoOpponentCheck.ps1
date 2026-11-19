[CmdletBinding()]
# The default bound is derived in LDDemoOpponentProbe.cpp (StartOpponentProbe): the flagpole opens at most 600 s after the
# start, plus the 600 s kept for the lobby, the capture contest and the results.
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(300,2400)][int]$TimeoutSeconds=1200,
    # Optional engine packet simulation for every role, e.g. '-PktLag=40 -PktLagVariance=10 -PktLoss=1'.
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation,
    # Optional log verbosity for every role, e.g. 'LogLDNav Verbose' or 'LogLDTrace off, LogLDCombat Display' (passed as -LogCmds).
    [ValidatePattern('^[A-Za-z0-9_]+ [A-Za-z]+(, ?[A-Za-z0-9_]+ [A-Za-z]+)*$')][string]$LogCmds,
    # DEV telemetry instead of the full-room check: one idle headless Listen Host and three AI play -MatchCount matches in a
    # row; each match gets -TimeoutSeconds. Writes one MatchNN.json per match and Summary.json; it judges nothing.
    [switch]$Telemetry, [ValidateRange(1,50)][int]$MatchCount=1,
    # Telemetry only: the Host advances a fixed 1/60 s step as fast as the machine allows (-UseFixedTimeStep -FPS=60, as
    # RunBuildEvaluation.ps1 does) instead of following the wall clock. -TimeoutSeconds stays wall-clock time per match.
    [switch]$Accelerated,
    # The interface part of the full-room check alone: the same three roles start the match, the rendered Host takes the
    # opening rest's review screenshots and layout checks, and the run ends there. Nobody plays the match out.
    [switch]$UIOnly,
    # Telemetry only: DEV content hot reload (-LDDevContentReload, editor build only), so a re-synced demo_content.json
    # is played without rebuilding C++. Each match records the content root it played.
    [switch]$DevContentReload)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
if ($Telemetry -and $NetEmulation) { throw 'Telemetry runs the Host alone on loopback; -NetEmulation has no connection to impair.' }
if (-not $Telemetry -and $PSBoundParameters.ContainsKey('MatchCount')) { throw '-MatchCount applies only to -Telemetry.' }
if ($Accelerated -and -not $Telemetry) { throw '-Accelerated applies only to -Telemetry; the full-room check runs on real connections in real time.' }
if ($UIOnly -and $Telemetry) { throw '-UIOnly is the interface part of the full-room check; it does not combine with -Telemetry.' }
if ($DevContentReload -and (-not $Telemetry -or $PackageRoot)) { throw '-DevContentReload applies only to -Telemetry on the editor build; a package must be cooked again.' }
$scenario = if ($Telemetry) { 'DemoOpponent-Telemetry' + $(if ($Accelerated) { '-Accelerated' } else { '' }) }
    elseif ($UIOnly) { 'DemoOpponent-UI' } else { 'DemoOpponent-FullRoom' }
$output = New-LDArtifactDirectory ($scenario + $(if ($PackageRoot) { '-Packaged' } else { '' }) +
    $(if ($NetEmulation) { '-NetEmulated' } else { '' }))
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds + 60) -PollMilliseconds 200 `
    -ExitError { param($process) "Opponent scenario process exited before evidence completed: PID $($process.Id); $(Get-OpponentFailure). Evidence: $output" } `
    -TimeoutError { "Opponent scenario evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire
$processes = $harness.Processes

function Start-OpponentRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        # The Host renders (1280x720 offscreen) so it can keep HUD screenshots of the match; clients and the telemetry
        # Host stay headless.
        $portArgument = if ($Role -eq 'Host') { "-windowed -RenderOffScreen -ResX=1280 -ResY=720 -port=$port" }
            elseif ($Role -eq 'Telemetry') { "-nullrhi -port=$port -LDDemoOpponentMatches=$MatchCount$(if ($DevContentReload) { ' -LDDevContentReload' })$(if ($Accelerated) { ' -UseFixedTimeStep -FPS=60' })" }
            else { '-nullrhi' }
        $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoOpponentProbe -LDDemoOpponentRole={2} -LDDemoOpponentTimeout={3} -LDDemoOpponentOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,$log
        if ($NetEmulation) { $arguments += " $NetEmulation" }
        if ($UIOnly) { $arguments += ' -LDDemoOpponentUIOnly' }
        if ($LogCmds) { $arguments += " -LogCmds=""$LogCmds""" }
        $arguments
    }
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

# Every failed check of a role's report with its detail (widget rectangles for a layout check), one per line.
function Get-OpponentFailedChecks {
    param($Report)
    @($Report.checks | Where-Object { $_.status -eq 'failed' } | ForEach-Object {
        # detail is optional (only interface checks write it); StrictMode refuses a missing property.
        $detail = $_.PSObject.Properties['detail']
        "FAILED: $($_.name)$(if ($detail -and $detail.Value) { " -- $($detail.Value)" })" })
}

function Invoke-OpponentUI {
    Start-OpponentRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoOpponentHost'
    Wait-HostLobby
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoOpponentClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-OpponentRole "Client$index" $url
    }
    # A failing Host writes its report and exits with a non-zero code, so wait on the report rather than on the harness'
    # early-exit guard: every failed check is then read from the report itself.
    $file = Join-Path $evidence 'Host.json'
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while (-not (Test-Path -LiteralPath $file)) {
        $processes[0].Refresh()
        if ($processes[0].HasExited -and -not (Test-Path -LiteralPath $file)) { throw "Opponent interface check Host exited without a report. Evidence: $output" }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Opponent interface check timed out: $output" }
        Start-Sleep -Milliseconds 200
    }
    $report = Read-LDDemoReport -Harness $harness -Name 'Host.json'
    $failed = @(Get-OpponentFailedChecks $report)
    foreach ($line in $failed) { Write-Host $line }
    if (-not $report.passed -or $failed.Count) {
        throw "Opponent interface check failed with $($failed.Count) failed check(s), all listed above: $($report.detail). Evidence: $output"
    }
    Stop-LDDemoClients -Harness $harness -Message 'Interface review done' -ExitError "Opponent interface check client did not exit cleanly: $output"
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness)
    if ($errors.Count) { throw "Opponent interface check logged an error: $($errors[0].Line). Evidence: $output" }
    $interface = @($report.checks | Where-Object { $_.name -like 'UI: *' })
    Write-Host "DEV-only opening-rest interface check passed: $($interface.Count) interface check(s) and $(@($report.summary.screenshots).Count) review screenshot(s) at 1280x720 and 1920x1080, text scale 100% and 125%, on a real Listen Host with two owning clients and three AI. The match was not played out; the screenshots still need a human look. Evidence: $output"
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
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.WaitForExit((($TimeoutSeconds * $MatchCount) + 60) * 1000)) { throw "Opponent telemetry timed out: $output" }
    $file = Join-Path $evidence 'Telemetry.json'
    if (-not (Test-Path -LiteralPath $file)) { throw "Opponent telemetry left no report (exit $($process.ExitCode)): $output" }
    $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    if (-not $report.passed -or $process.ExitCode -ne 0) {
        throw "Opponent telemetry failed after $($report.matches_completed) of $MatchCount match(es): $($report.detail). Evidence: $output"
    }
    # The nightly packaging gate: an Error or Fatal line of any category fails the run, not only the game's own. Python
    # lines are the editor plugins' own start-up noise and stay out, as in the other scenarios.
    $errors = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'Fatal error|Assertion failed|Log(?!Python:)\w+: (Error|Fatal):|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Opponent telemetry logged an error: $($errors[0].Line). Evidence: $output" }
    Invoke-LDTool -Tool 'python' -ToolArguments @('-B', (Join-Path $PSScriptRoot 'SummarizeOpponentTelemetry.py'), $evidence) -LogPath (Join-Path $output 'Summary.txt')
    $played = [math]::Round((@($report.files | ForEach-Object { (Get-Content -LiteralPath (Join-Path $evidence $_) -Raw | ConvertFrom-Json).duration_ms }) | Measure-Object -Sum).Sum / 1000)
    $step = if ($report.fixed_time_step) { "a fixed $([math]::Round(1 / $report.fixed_delta_seconds)) steps/s, unrendered" } else { 'real time' }
    Write-Host "Telemetry pace: $played s of match time in $([int]$watch.Elapsed.TotalSeconds) s of wall time including start-up ($step)."
    Write-Host "DEV-only opponent telemetry recorded $MatchCount match(es) of one idle Host and three AI. It describes these matches; it is not an AI acceptance check or a balance verdict. Evidence: $output"
}

try {
    if ($Telemetry) { Invoke-OpponentTelemetry; return }
    if ($UIOnly) { Invoke-OpponentUI; return }
    Start-OpponentRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoOpponentHost'
    Wait-HostLobby
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoOpponentClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-OpponentRole "Client$index" $url
    }
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $r = Read-LDDemoReport -Harness $harness -Name "$_.json"
        # A check this match gave no occasion for is listed below as not exercised; it is neither a pass nor a failure.
        if (-not $r.passed -or $r.role -ne $_ -or @($r.checks | Where-Object { $_.status -ne 'passed' -and $_.status -ne 'not_exercised' }).Count -ne 0) {
            foreach ($line in (Get-OpponentFailedChecks $r)) { Write-Host "$_ $line" }
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
    Stop-LDDemoClients -Harness $harness -Message 'Disconnect owning clients' -ExitError "Opponent scenario client did not exit cleanly: $output"
    Stop-LDDemoHost -Harness $harness
    $loggedErrors = @(Get-LDDemoLogErrors -Harness $harness)
    $errors = $loggedErrors
    if ($errors.Count) { throw "Opponent scenario logged an error: $($errors[0].Line). Evidence: $output" }
    Assert-LDNetEmulationApplied -Output $output -Roles @('Host','Client1','Client2') -NetEmulation $NetEmulation
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
    Close-LDDemoHarness -Harness $harness
}
