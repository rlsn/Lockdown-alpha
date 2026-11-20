[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(120,1800)][int]$TimeoutSeconds=1800,
    [switch]$AllHuman, [switch]$RenderHost, [switch]$NoScreenshots)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory (($(if ($PackageRoot) { 'DemoCapture-OwnedMatch-Packaged' } else { 'DemoCapture-OwnedMatch' })) +
    $(if ($AllHuman) { '-SixOwners' } else { '' }))
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds + 60) -PollMilliseconds 200 `
    -ExitError { param($process) "Capture scenario process exited before evidence completed: PID $($process.Id); $(Get-CaptureFailure). Evidence: $output" } `
    -TimeoutError { "Capture scenario evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire
# Each role reports exactly these stable check ids, once each and passed. A missing, extra or repeated id means
# the probe and this runner drifted apart; T01/P01 only appear on failure paths and therefore always fail.
$requiredChecks = @{
    Host = @('C01','C02','C03','C04','C04F','C04P','C05','C06','C07','C08','C09','C10','C11','C12','C13','A01','A02','A03')
    Client1 = @('C01','C02','C03','C04','C04F','C04P','C05','C06','C07','C08','C09','C10','C11','C12','C13')
}
$requiredChecks.Client2 = $requiredChecks.Client1
$clientCount = if ($AllHuman) { 5 } else { 2 }
$roles = @('Host') + @(1..$clientCount | ForEach-Object { "Client$_" })
foreach ($role in $roles | Where-Object { $_ -ne 'Host' }) { $requiredChecks[$role] = $requiredChecks.Client1 }
if ($AllHuman) {
    $requiredChecks.Host += 'A04'
    $requiredChecks.Client5 = $requiredChecks.Client5 + 'C14'
}
if ($RenderHost -and -not $NoScreenshots) { $requiredChecks.Host += 'C15' }
# The names each role chooses in its ?LDName= option and the probe expects on the roster and the results (the
# Host's has one inner space). They are built from code points so this script stays ASCII.
function Get-CaptureName {
    param([Parameter(Mandatory)][string]$Role)
    $points = @{ Host = 0x62A2,0x5708,0x20,0x623F,0x4E3B; Client1 = 0x62A2,0x5708,0x7532;
        Client2 = 0x62A2,0x5708,0x4E59; Client3 = 0x62A2,0x5708,0x4E19;
        Client4 = 0x62A2,0x5708,0x4E01; Client5 = 0x62A2,0x5708,0x620A }[$Role]
    -join ($points | ForEach-Object { [char]$_ })
}

function Start-CaptureRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
        # UI commands need initialized rendered widgets on every owner; only the Host saves frames.
        $render = if ($RenderHost) {
            '-RenderOffScreen -ResX=1280 -ResY=720' + $(if ($Role -eq 'Host' -and -not $NoScreenshots) { ' -LDCaptureRenderHost' } else { '' })
        } else { '-nullrhi' }
        $arguments = '-unattended -nop4 -nosound {0} -multihome=127.0.0.1 {1} -UserDir="{2}" -LDDemoCaptureProbe -LDDemoCaptureRole={3} -LDDemoCaptureTimeout={4} -LDDemoCaptureOutput="{5}" -abslog="{6}"' -f $render,$portArgument,$user,$Role,$TimeoutSeconds,$evidence,$log
        if ($AllHuman) { $arguments += ' -LDCaptureAllHuman' }
        $arguments
    }
}

function Get-CaptureFailure {
    foreach ($role in $roles) {
        $log = Join-Path $output "$role.log"
        if (Test-Path -LiteralPath $log) {
            $networkFailure = Select-String -LiteralPath $log -SimpleMatch 'LogNet: Error: UEngine::BroadcastNetworkFailure' | Select-Object -First 1
            if ($networkFailure -and -not ($AllHuman -and $role -eq 'Client5')) { return "Network failure for $role`: $($networkFailure.Line). Evidence: $output" }
        }
        $file = Join-Path $evidence "$role.json"
        if (Test-Path -LiteralPath $file) {
            $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (-not $report.passed) { return "$role reported: $($report.detail)" }
        }
    }
    return 'no role report explains it'
}

try {
    Start-CaptureRole 'Host' ('/Game/Lockdown/Generated/Maps/L_Demo?listen?LDName={0}' -f (Get-CaptureName 'Host'))
    Wait-LDDemoEvidence -Harness $harness -Names @('HostJoined.json')
    foreach ($index in 1..$clientCount) {
        $url = '127.0.0.1:{0}?LDName={1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,(Get-CaptureName "Client$index"),$wire,$version,$digest
        Start-CaptureRole "Client$index" $url
    }
    Wait-LDDemoEvidence -Harness $harness -Names @($roles | ForEach-Object { "$_.json" })
    $reports = foreach ($role in $roles) {
        $r = Read-LDDemoReport -Harness $harness -Name "$role.json"
        if (-not $r.passed -or $r.role -ne $role -or @($r.checks | Where-Object { $_.passed -ne $true }).Count -ne 0) {
            throw "Owned capture evidence failed for $role`: $($r.detail). Evidence: $output"
        }
        $ids = @($r.checks | ForEach-Object { [string]$_.id })
        $missing = @($requiredChecks[$role] | Where-Object { $ids -cnotcontains $_ })
        $unexpected = @($ids | Where-Object { $requiredChecks[$role] -cnotcontains $_ })
        $repeated = @($ids | Group-Object -CaseSensitive | Where-Object Count -gt 1 | ForEach-Object Name)
        if ($missing.Count -or $unexpected.Count -or $repeated.Count) {
            throw "Capture checks for $role differ from the required ids: missing [$($missing -join ' ')], unexpected [$($unexpected -join ' ')], repeated [$($repeated -join ' ')]. Evidence: $output"
        }
        $r
    }
    if ($RenderHost -and -not $NoScreenshots) {
        foreach ($image in @('HostFinalOffer.png','HostFinalLocked.png','HostFinalFlag.png','HostResults.png','HostReturnedLobby.png')) {
            $path = Join-Path $evidence $image
            if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -eq 0) {
                throw "Missing rendered walkthrough frame: $path"
            }
        }
    }
    $winner = $reports[0].summary.winner
    foreach ($r in $reports) {
        if ($r.summary.winner -ne $winner -or $r.summary.ended_at_ms -ne $reports[0].summary.ended_at_ms -or
            $r.summary.next_generation -ne $reports[0].summary.next_generation -or
            -not $r.summary.second_generation -or $r.summary.second_generation -ne $reports[0].summary.second_generation) {
            throw "Roles disagree on the unique result, the next Lobby generation or the second match generation: $output"
        }
    }
    Stop-LDDemoClients -Harness $harness -Message 'Disconnect owning clients' -ExitError "Capture client did not exit cleanly: $output"
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness |
        Where-Object { -not ($AllHuman -and (Split-Path -Leaf $_.Path) -eq 'Client5.log' -and
            $_.Line -match 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost,') })
    if ($errors.Count) { throw "Capture scenario logged an error: $($errors[0].Line). Evidence: $output" }
    $roster = if ($AllHuman) { 'six real owners, no AI' } else { 'three real owners plus three production AI' }
    Write-Host ("Normal-start six-contender showdown passed: {0}, three growth fields and final rest, six independent FIN choices and locks, distinct final entries, objective deadline/result, growth freeze, Lobby return and fresh second start in generation {1}. Six-owner mode also verifies frontend reconnect after selection. Not graphical quality or exhaustive respawn choreography. Evidence: {2}" -f
        $roster,$reports[0].summary.second_generation,$output)
}
finally {
    Close-LDDemoHarness -Harness $harness
}
