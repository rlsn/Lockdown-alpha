[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(120,600)][int]$TimeoutSeconds=300,
    # The Host's rendered main view; the HUD and screenshots are reviewed at both supported sizes.
    [ValidateSet('720p','1080p')][string]$Resolution='720p',
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation,
    [switch]$RenderClient1, [switch]$ReactionOnly, [switch]$IceOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
if (($ReactionOnly -or $IceOnly) -and $RenderClient1) { throw 'ReactionOnly and IceOnly are headless; omit RenderClient1.' }
if ($ReactionOnly -and $IceOnly) { throw 'Choose one of ReactionOnly and IceOnly.' }
$headless = $ReactionOnly -or $IceOnly
Assert-LDDemoInputs
$output = New-LDArtifactDirectory ($(if ($PackageRoot) { 'DemoVision-OwnedObservation-Packaged' } else { 'DemoVision-OwnedObservation' }) +
    $(if ($ReactionOnly) { '-Reaction' } elseif ($IceOnly) { '-Ice' } else { '' }) +
    $(if ($Resolution -ne '720p') { "-$Resolution" } else { '' }))
$resolutionArguments = if ($Resolution -eq '1080p') { '-ForceRes -ResX=1920 -ResY=1080' } else { '-ResX=1280 -ResY=720' }
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds) -PollMilliseconds 100 `
    -ExitError { param($process) "Observation process exited before its evidence completed: PID $($process.Id). Evidence: $output" } `
    -TimeoutError { "Observation evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire
$roles = @('Host','Client1','Client2')
# Every role must report the check groups it owns; a skipped stage cannot pass silently.
$requiredChecks = @{ Host = @('#1','#15H','#23'); Client1 = @('#1','#15','#23'); Client2 = @('#1','#15','#23') }
if ($ReactionOnly) {
    $requiredChecks = @{ Host = @('#1','#Reaction'); Client1 = @('#1','#15','#Reaction'); Client2 = @('#1','#15','#Reaction') }
}
if ($IceOnly) {
    $requiredChecks = @{ Host = @('#1','#Ice'); Client1 = @('#1','#15','#Ice'); Client2 = @('#1','#15') }
}

function Start-VisionRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        # The Host is a participating Listen Host with a real RHI main view; owning clients need no renderer.
        $render = if (-not $headless -and ($Role -eq 'Host' -or ($Role -eq 'Client1' -and $RenderClient1))) {
            "-windowed -RenderOffScreen $resolutionArguments -LDDemoVisionRendered"
        } else { '-nullrhi' }
        if ($Role -eq 'Host') { $render += " -port=$port" }
        $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoVisionProbe -LDDemoVisionRole={2} -LDDemoVisionTimeout={3} -LDDemoVisionOutput="{4}" -abslog="{5}"' -f $render,$user,$Role,$TimeoutSeconds,$evidence,$log
        if ($NetEmulation) { $arguments += " $NetEmulation -LDDemoVisionNetEmulated" }
        if ($ReactionOnly) { $arguments += ' -LDDemoVisionReactionOnly' }
        if ($IceOnly) { $arguments += ' -LDDemoVisionIceOnly' }
        $arguments
    }
}

try {
    Start-VisionRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoVisionHost'
    Wait-LDDemoEvidence -Harness $harness -Names @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoVisionClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-VisionRole "Client$index" $url
    }
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json','Client2.json')
    $reports = @{}
    foreach ($role in $roles) {
        $report = Read-LDDemoReport -Harness $harness -Name "$role.json"
        $failed = @($report.checks | Where-Object { -not $_.passed })
        if (-not $report.passed -or $report.role -ne $role -or -not $report.normal_start -or
            $report.catalog_sha256 -ne $digest -or $failed.Count -ne 0) {
            throw "Owned observation evidence failed for $role`: $output"
        }
        foreach ($id in $requiredChecks[$role]) {
            if (@($report.checks | Where-Object { $_.id -eq $id }).Count -eq 0) { throw "$role report lacks its $id checks: $output" }
        }
        $reports[$role] = $report
    }
    foreach ($role in $roles) {
        if ($reports[$role].generation -ne $reports['Host'].generation) { throw "Roles observed different match generations: $output" }
    }
    $image = Join-Path $evidence 'HostSighted.png'
    if (-not $headless -and (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).Length -le 0)) {
        throw "The participating Listen Host left no rendered main-view screenshot: $output"
    }
    if ($RenderClient1) {
        $clientImage = Join-Path $evidence 'Client1Sighted.png'
        if (-not (Test-Path -LiteralPath $clientImage) -or (Get-Item -LiteralPath $clientImage).Length -le 0) {
            throw "The rendered owning client left no screenshot: $output"
        }
    }
    Stop-LDDemoClients -Harness $harness -Message 'Leave room' -ExitError "Owning client did not exit cleanly: $output"
    Wait-LDDemoEvidence -Harness $harness -Names @('Disconnects.json')
    $disconnect = Read-LDDemoReport -Harness $harness -Name 'Disconnects.json'
    if (-not $disconnect.passed -or $disconnect.departed_contenders -ne 2) {
        throw "The Listen Host still presents departed contenders: $output"
    }
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'Fatal error|Assertion failed|Ensure condition failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    $pythonErrors = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'LogPython: Error:')
    if ($pythonErrors.Count) {
        Write-Warning "Unreal Python also logged $($pythonErrors.Count) error lines; inspect the role logs separately. Evidence: $output"
    }
    if ($IceOnly) {
        Write-Host "DEV-only owned-connection ice scenario passed: the Host's ice field under Client1 scales the server body's acceleration, the owning client predicts at the same scale and its walk across the ice stays within the correction allowance. Not balance or other ground states. Evidence: $output"
    } elseif ($ReactionOnly) {
        Write-Host "DEV-only owned-connection reaction scenario passed: fixture hits enter native Effects, only the setter receives the named prompt, and explicit leavers are removed from the Host view. Not player input, hidden-sight or unexpected-disconnect coverage. Evidence: $output"
    } else {
        Write-Host "DEV-only owned-connection observation scenario passed: rendered Listen Host plus two owning clients via the normal lobby. Covers checks #1/#9/#15/#15H/#23/#28 (Listen Host presentation). HostSighted.png still needs presentation review. Not combat balance, death/termination, AI or full-match acceptance. Evidence: $output"
    }
}
finally {
    Close-LDDemoHarness -Harness $harness
}
