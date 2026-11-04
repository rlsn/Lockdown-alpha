[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(120,600)][int]$TimeoutSeconds=300,
    # The Host's rendered main view; the HUD and screenshots are reviewed at both supported sizes.
    [ValidateSet('720p','1080p')][string]$Resolution='720p')
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory ($(if ($PackageRoot) { 'DemoVision-OwnedObservation-Packaged' } else { 'DemoVision-OwnedObservation' }) +
    $(if ($Resolution -ne '720p') { "-$Resolution" } else { '' }))
$resolutionArguments = if ($Resolution -eq '1080p') { '-ForceRes -ResX=1920 -ResY=1080' } else { '-ResX=1280 -ResY=720' }
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
$roles = @('Host','Client1','Client2')
# Every role must report the check groups it owns; a skipped stage cannot pass silently. #ST is the paper box stealth
# segment (four trials, each role hides once or observes); #STH is the Listen Host's presentation of a stealthed body.
$requiredChecks = @{ Host = @('#1','#15H','#23','#ST','#STH'); Client1 = @('#1','#15','#23','#ST'); Client2 = @('#1','#15','#23','#ST') }
$stealthTrials = 4
# Only the Host runs the DEV growth fixture: it stages the clients' opening offer and grants the boxes' gold, with no RPC.
$fixtureSwitch = '-LDDevGrowthFixture'

function Start-VisionRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    # The Host is a participating Listen Host with a real RHI main view; owning clients need no renderer.
    $render = if ($Role -eq 'Host') { "-windowed -RenderOffScreen $resolutionArguments -port=$port $fixtureSwitch" } else { '-nullrhi' }
    $arguments = '-unattended -nop4 -nosound -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoVisionProbe -LDDemoVisionRole={2} -LDDemoVisionTimeout={3} -LDDemoVisionOutput="{4}" -abslog="{5}"' -f $render,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Wait-VisionFiles {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Observation process exited before its evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Observation evidence timed out: $output"
}

try {
    Start-VisionRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoVisionHost'
    Wait-VisionFiles @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoVisionClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-VisionRole "Client$index" $url
    }
    Wait-VisionFiles @('Host.json','Client1.json','Client2.json')
    $reports = @{}
    foreach ($role in $roles) {
        $report = Get-Content -LiteralPath (Join-Path $evidence "$role.json") -Raw | ConvertFrom-Json
        $failed = @($report.checks | Where-Object { -not $_.passed })
        if (-not $report.passed -or $report.role -ne $role -or -not $report.normal_start -or
            $report.catalog_sha256 -ne $digest -or $failed.Count -ne 0) {
            throw "Owned observation evidence failed for $role`: $output"
        }
        foreach ($id in $requiredChecks[$role]) {
            if (@($report.checks | Where-Object { $_.id -eq $id }).Count -eq 0) { throw "$role report lacks its $id checks: $output" }
        }
        if ($report.growth_fixture -ne ($role -eq 'Host')) { throw "Only the Host may run with the DEV growth fixture: $output" }
        $trials = @($report.stealth)
        if ($trials.Count -ne $stealthTrials -or @($trials | Where-Object { $_.leaks }).Count -ne 0) {
            throw "$role did not finish every stealth trial without a leak: $output"
        }
        $reports[$role] = $report
    }
    foreach ($role in $roles) {
        if ($reports[$role].generation -ne $reports['Host'].generation) { throw "Roles observed different match generations: $output" }
    }
    $image = Join-Path $evidence 'HostSighted.png'
    if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).Length -le 0) {
        throw "The participating Listen Host left no rendered main-view screenshot: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Owning client did not exit cleanly: $output" }
    }
    Wait-VisionFiles @('Disconnects.json')
    $disconnect = Get-Content -LiteralPath (Join-Path $evidence 'Disconnects.json') -Raw | ConvertFrom-Json
    if (-not $disconnect.passed -or $disconnect.departed_contenders -ne 2) {
        throw "The Listen Host still presents departed contenders: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    if (-not (Select-String -LiteralPath (Join-Path $output 'Host.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE' -Quiet) -or
        @(Select-String -LiteralPath (Join-Path $output 'Client1.log'),(Join-Path $output 'Client2.log') -Pattern 'LD_DEV_FIXTURE_ACTIVE').Count) {
        throw "The DEV growth fixture must be active on the Host and absent from both clients: $output"
    }
    # The final report names every scenario that ran a DEV fixture.
    [ordered]@{ fixture = 'growth'; switch = $fixtureSwitch; roles = @('Host') } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $output 'DevFixture.json') -Encoding UTF8
    $stealth = foreach ($role in $roles) { foreach ($row in $reports[$role].stealth) {
        "S$($row.trial) $($row.trigger) $role/$($row.role_kind): frames=$($row.frames_while_hidden)" +
            $(if ($row.role_kind -eq 'near') { " rediscovered=$([math]::Round($row.rediscovered_cm))cm" } elseif ($row.role_kind -eq 'far') { " regained=$($row.expires_ms - $row.regained_sample_ms)ms-before-expiry" } else { " ended=$($row.expires_ms - $row.revealed_ms)ms-before-expiry" }) } }
    Write-Host ("Stealth trials: " + ($stealth -join '; '))
    Write-Host "DEV-only owned-connection observation scenario passed: rendered Listen Host plus two owning clients via the normal lobby. Covers checks #1/#9/#15/#23/#28, Listen Host presentation and the paper box stealth segment (#ST/#STH: no frame, handle, proxy or Host body leak beyond the reveal radius; lost lock and chase cancelled; fresh handle on rediscovery; reveal by attack, cast, hit and taunt). DEV growth fixture: Host only ($fixtureSwitch). HostSighted.png still needs presentation review. Not combat balance, death/termination, AI or full-match acceptance. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
