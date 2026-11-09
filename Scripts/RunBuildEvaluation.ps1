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
$runner = [ordered]@{ fixed_fps = $FixedFps; exit_code = $null; timed_out = $false; detail = ''; log_errors = @() }
if ($planned -eq 0) {
    $launch = Get-LDGameLaunch -EngineRoot $EngineRoot -Output $run
    if ($launch.Packaged) { throw 'Build evaluation runs on the editor build.' }
    $native = Join-Path $run 'native'
    $user = Join-Path $run 'Host-User'
    New-Item -ItemType Directory -Force -Path $native, $user | Out-Null
    $budget = (Get-Content -LiteralPath (Join-Path $run 'manifest.json') -Raw | ConvertFrom-Json).budget.run_wall_timeout_s
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
    $port = $socket.Client.LocalEndPoint.Port
    $socket.Dispose()
    $log = Join-Path $run 'Host.log'
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 -port={0} -UserDir="{1}" -LDBuildEvaluation -LDDevGrowthFixture -LDBuildEvalInput="{2}" -LDBuildEvalOutput="{3}" -UseFixedTimeStep -FPS={4} -abslog="{5}"' -f $port,$user,(Join-Path $run 'native_input.json'),$native,$FixedFps,$log
    $process = Start-Process -FilePath $launch.Binary -WindowStyle Hidden -PassThru -ArgumentList (Format-LDGameArguments -Launch $launch `
        -Url '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=BuildEvaluationHost' -Arguments $arguments)
    # The manifest's run budget covers the Cases; startup and the map load get a fixed allowance on top.
    if (-not $process.WaitForExit(([int]$budget + 300) * 1000)) {
        Stop-Process -Id $process.Id
        $runner.timed_out = $true
        $runner.detail = "the run exceeded its wall budget of $budget s and was stopped"
    }
    $process.Refresh()
    $runner.exit_code = $process.ExitCode
    if (Test-Path -LiteralPath $log) {
        $runner.log_errors = @(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT' |
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
if ($runner.timed_out -or ($planned -eq 0 -and $runner.exit_code -ne 0)) { throw "The native evaluation Host did not finish cleanly ($($runner.detail), exit $($runner.exit_code)). Evidence: $run" }
if ($analyzed -ne 0) { throw "The evaluation found a rule violation (see findings). Evidence: $run" }
if ($replayed -ne 0) { throw "The replayed Case diverged from its source. Evidence: $run" }
Write-Host "DEV-only build evaluation finished. It reports effects within the covered builds, strategy, scenario and budget only; it is not a balance verdict. Evidence: $run"
