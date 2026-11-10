[CmdletBinding()]
param(
    # A build evaluation manifest (docs/systems/BUILD_EVALUATION.md); every budget field is explicit in it.
    [string]$Manifest,
    # Replay one recorded Case of an earlier run in a new process and compare it with the original.
    [string]$ReplayRun, [string]$Case,
    # Validate, admit and expand the Cases only; nothing native is started.
    [switch]$PlanOnly,
    [string]$EngineRoot,
    # Fixed simulation step of the evaluation Host (-UseFixedTimeStep -FPS=N): the same Case replays the same frames.
    [ValidateRange(20,240)][int]$FixedFps = 60,
    # Replay only: largest allowed heartbeat position deviation.
    [ValidateRange(0,1000)][double]$SpatialToleranceCm = 0)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
if ([bool]$Manifest -eq [bool]$ReplayRun) { throw 'Give either -Manifest, or -ReplayRun with -Case.' }
if ($ReplayRun -and -not $Case) { throw '-ReplayRun needs -Case.' }
if ($PlanOnly -and $ReplayRun) { throw '-PlanOnly applies to a manifest.' }
Assert-LDDemoInputs
$core = Join-Path $PSScriptRoot 'BuildEvaluation.py'
$evaluations = Join-Path (Get-LDProjectRoot) 'Artifacts\BuildEvaluation'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ($Manifest) {
    $Manifest = (Resolve-Path -LiteralPath $Manifest).Path
    $experiment = (Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json).experiment.id
    if ($experiment -notmatch '^[A-Za-z0-9_][A-Za-z0-9_.-]{0,79}$') { throw "The manifest's experiment.id is not an identifier." }
    $run = Join-Path $evaluations "$stamp-$experiment"
    & python -B $core plan --manifest $Manifest --run-dir $run
} else {
    $ReplayRun = (Resolve-Path -LiteralPath $ReplayRun).Path
    if ($Case -notmatch '^[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}$') { throw '-Case is not a Case id.' }
    $run = Join-Path $evaluations "$stamp-replay-$Case"
    & python -B $core replay-plan --source $ReplayRun --case $Case --run-dir $run
}
$planned = $LASTEXITCODE
if ($planned -eq 2 -or ($planned -ne 0 -and $planned -ne 3)) { throw "The build evaluation plan was refused (exit $planned); nothing was started." }
if ($PlanOnly) {
    Write-Host "Plan only: Cases, coverage and native input are in $run. Nothing native was started."
    return
}
$runner = [ordered]@{ fixed_fps = $FixedFps; exit_code = $null; timed_out = $false; detail = ''; log_errors = @()
    binary_digests = $null; binary_digests_after = $null; budget_stop = $null; disk = $null }
# Startup and the map load get a fixed allowance on top of the manifest's run budget, which covers the Cases.
$startupAllowanceSeconds = 300
# A Case's authoritative evaluation events are the LD_* lines of these categories between its begin and end markers
# (LDBuildEvaluationProbe's trace capture); the Host log is read as it grows to stop a Case past its event budget.
$eventLine = [regex]'(?:^|\])Log(?:LDCombat|LDGrowth|LDMatch|LDAI|LDTrace|LDTest): (?:\w+: )?LD_'
$caseBegin = [regex]'LD_EVAL_CASE_BEGIN case=(\S+)'
$caseEnd = [regex]'LD_EVAL_CASE_END '
function Read-LDEvalEvents([string]$Path, $State, [int]$Limit) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]'ReadWrite, Delete')
    try {
        if ($stream.Length -le $State.offset) { return $null }
        $stream.Position = $State.offset
        $buffer = [byte[]]::new([Math]::Min($stream.Length - $State.offset, 16MB))
        $read = $stream.Read($buffer, 0, $buffer.Length)
    } finally { $stream.Dispose() }
    if ($read -le 0) { return $null }
    $end = [Array]::LastIndexOf($buffer, [byte]10, $read - 1)
    if ($end -lt 0) { if ($read -lt 16MB) { return $null }; $end = $read - 1 }
    $State.offset += $end + 1
    foreach ($line in [Text.Encoding]::UTF8.GetString($buffer, 0, $end + 1).Split("`n")) {
        $begin = $caseBegin.Match($line)
        if ($begin.Success) { $State.case = $begin.Groups[1].Value; $State.events = 0 }
        elseif ($caseEnd.IsMatch($line)) { $State.case = $null; continue }
        if ($State.case -and $eventLine.IsMatch($line)) {
            $State.events++
            if ($State.events -gt $Limit) {
                return [ordered]@{ gate = 'EventBudget'; case = $State.case; first_over_limit = $line.TrimEnd()
                    detail = "the Case logged more than max_case_events $Limit authoritative events; the Host was stopped and its log kept" }
            }
        }
    }
    return $null
}
function Invoke-LDEvalDiskGate {
    $output = & python -B $core gate --run-dir $run
    $code = $LASTEXITCODE
    if ($code -ne 0 -and $code -ne 4) { return @{ usage = $null; failure = "the disk gate could not measure the run (exit $code)" } }
    return @{ usage = ($output | Select-Object -Last 1 | ConvertFrom-Json); failure = $null }
}
if ($planned -eq 0) {
    $launch = Get-LDGameLaunch -EngineRoot $EngineRoot -Output $run
    if ($launch.Packaged) { throw 'Build evaluation runs on the editor build.' }
    $module = Join-Path (Get-LDProjectRoot) 'Binaries\Win64\UnrealEditor-Lockdown.dll'
    $runner.binary_digests = [ordered]@{
        editor_sha256 = (Get-FileHash -LiteralPath $launch.Binary -Algorithm SHA256).Hash.ToLowerInvariant()
        module_sha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $native = Join-Path $run 'native'
    $user = Join-Path $run 'Host-User'
    New-Item -ItemType Directory -Force -Path $native, $user | Out-Null
    $budget = (Get-Content -LiteralPath (Join-Path $run 'manifest.json') -Raw | ConvertFrom-Json).budget
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
    $port = $socket.Client.LocalEndPoint.Port
    $socket.Dispose()
    $log = Join-Path $run 'Host.log'
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 -port={0} -UserDir="{1}" -LDBuildEvaluation -LDDevGrowthFixture -LDBuildEvalInput="{2}" -LDBuildEvalOutput="{3}" -UseFixedTimeStep -FPS={4} -abslog="{5}"' -f $port,$user,(Join-Path $run 'native_input.json'),$native,$FixedFps,$log
    $process = Start-Process -FilePath $launch.Binary -WindowStyle Hidden -PassThru -ArgumentList (Format-LDGameArguments -Launch $launch `
        -Url '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=BuildEvaluationHost' -Arguments $arguments)
    # One Host, Cases in order. The first gate reached stops it; its reason is kept and old evidence is never deleted.
    $wallLimit = [int]$budget.run_wall_timeout_s + $startupAllowanceSeconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $tail = @{ offset = [long]0; case = $null; events = 0 }
    $nextDiskCheck = 0
    while (-not $process.WaitForExit(1000)) {
        $stop = $null
        if ($clock.Elapsed.TotalSeconds -gt $wallLimit) {
            $stop = [ordered]@{ gate = 'RunWall'; case = $tail.case
                detail = "the run exceeded its wall budget of $($budget.run_wall_timeout_s) s plus $startupAllowanceSeconds s startup and was stopped" }
        }
        if (-not $stop) { $stop = Read-LDEvalEvents -Path $log -State $tail -Limit ([int]$budget.max_case_events) }
        if (-not $stop -and $clock.Elapsed.TotalSeconds -ge $nextDiskCheck) {
            $nextDiskCheck = $clock.Elapsed.TotalSeconds + 10
            $gate = Invoke-LDEvalDiskGate
            if ($gate.failure -or $gate.usage.exceeded) {
                $stop = [ordered]@{ gate = 'DiskBudget'; case = $tail.case; detail = $(if ($gate.failure) { $gate.failure } else { $gate.usage.exceeded }) }
            }
        }
        if ($stop) {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
            $runner.budget_stop = $stop
            $runner.timed_out = $stop.gate -eq 'RunWall'
            $runner.detail = "$($stop.gate): $($stop.detail)"
            break
        }
    }
    $process.Refresh()
    $runner.exit_code = $process.ExitCode
    # The last bytes the Host wrote between two checks still count against the disk budget.
    $gate = Invoke-LDEvalDiskGate
    $runner.disk = $gate.usage
    if (-not $runner.budget_stop -and ($gate.failure -or $gate.usage.exceeded)) {
        $runner.budget_stop = [ordered]@{ gate = 'DiskBudget'; case = $null
            detail = "after the native run: $(if ($gate.failure) { $gate.failure } else { $gate.usage.exceeded })" }
        $runner.detail = "DiskBudget: $($runner.budget_stop.detail)"
    }
    $runner.binary_digests_after = [ordered]@{
        editor_sha256 = (Get-FileHash -LiteralPath $launch.Binary -Algorithm SHA256).Hash.ToLowerInvariant()
        module_sha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    if ($runner.binary_digests.editor_sha256 -ne $runner.binary_digests_after.editor_sha256 -or
        $runner.binary_digests.module_sha256 -ne $runner.binary_digests_after.module_sha256) {
        $runner.log_errors += 'The evaluation binary changed during the native run.'
    }
    if (Test-Path -LiteralPath $log) {
        $runner.log_errors += @(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT' |
            Select-Object -First 20 | ForEach-Object { $_.Line })
    }
} else {
    $runner.detail = 'no runnable Case: every arm was refused at admission'
}
[IO.File]::WriteAllText((Join-Path $run 'runner.json'), ($runner | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
& python -B $core analyze --run-dir $run
$analyzed = $LASTEXITCODE
$replayed = 0
if ($ReplayRun -and $planned -eq 0) {
    & python -B $core compare --source $ReplayRun --replay $run --case $Case --spatial-tolerance-cm $SpatialToleranceCm
    $replayed = $LASTEXITCODE
}
Write-Host "Report: $(Join-Path $run 'report.md')"
if ($analyzed -eq 2 -or $replayed -eq 2) { throw "The build evaluation evidence could not be read. Evidence: $run" }
if ($runner.budget_stop) { throw "A budget gate stopped the native evaluation ($($runner.detail)); unfinished Cases are BudgetExceeded. Evidence: $run" }
if ($runner.timed_out -or ($planned -eq 0 -and $runner.exit_code -ne 0)) { throw "The native evaluation Host did not finish cleanly ($($runner.detail), exit $($runner.exit_code)). Evidence: $run" }
if ($analyzed -ne 0) { throw "The evaluation found a rule violation (see findings). Evidence: $run" }
if ($replayed -ne 0) { throw "The replayed Case diverged from its source. Evidence: $run" }
Write-Host "DEV-only build evaluation finished. It reports effects within the covered builds, strategy, scenario and budget only; it is not a balance verdict. Evidence: $run"
