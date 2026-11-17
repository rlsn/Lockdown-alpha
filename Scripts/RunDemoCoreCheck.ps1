# #198: the default covers the opening rest R0 (180 s build + 5 s return, no PK sign-ups) before the field-1 walk.
[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(30,600)][int]$TimeoutSeconds=330, [switch]$EarlyRestReady, [switch]$ReplayFieldReturn, [switch]$FinalEntryOnly,
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoCore-OwnedBodies-Packaged' } else { 'DemoCore-OwnedBodies' })
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds) -PollMilliseconds 100 `
    -ExitError { param($process) "Native core process exited before evidence completed: PID $($process.Id). Evidence: $output" } `
    -TimeoutError { "Native core evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire

function Start-CoreRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
        # The NPC service is part of the laid-out workshop, so its owning client needs real Slate geometry.
        $render = if ($EarlyRestReady -and $Role -eq 'Client1') { '-windowed -RenderOffScreen -ResX=1280 -ResY=720' } else { '-nullrhi' }
        $arguments = '-unattended -nop4 -nosound {0} -multihome=127.0.0.1 {1} -UserDir="{2}" -LDDemoCoreProbe -LDDemoCoreRole={3} -LDDemoCoreTimeout={4} -LDDemoCoreOutput="{5}" -abslog="{6}"' -f $render,$portArgument,$user,$Role,$TimeoutSeconds,$evidence,$log
        if ($EarlyRestReady) { $arguments += ' -LDDemoCoreReady' }
        if ($ReplayFieldReturn) { $arguments += ' -LDDemoCoreReplayFieldReturn' }
        if ($NetEmulation) { $arguments += " $NetEmulation" }
        $arguments
    }
}

try {
    Start-CoreRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCoreHost'
    Wait-LDDemoEvidence -Harness $harness -Names @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCoreClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-CoreRole "Client$index" $url
    }
    if ($FinalEntryOnly) {
        Wait-LDDemoEvidence -Harness $harness -Names @('FinalEntry.json')
        $entry = Read-LDDemoReport -Harness $harness -Name 'FinalEntry.json'
        if (-not $entry.passed -or $entry.entries -ne 6) { throw "Final entry preparation failed: $($entry.detail). Evidence: $output" }
        Write-Host "All six final entries prepared through the authoritative spawn path. Evidence: $output"
        return
    }
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $r = Read-LDDemoReport -Harness $harness -Name "$_.json"
        # A blocked-placement retry fixture can retire an R0 life; every role still finishes on exactly its next field life.
        if (-not $r.passed -or $r.role -ne $_ -or -not $r.fixture_only -or $r.full_ct06 -or $r.catalog_sha256 -ne $digest -or
            $r.rest_life_before_field -lt 1 -or $r.life -ne ($r.rest_life_before_field + 1) -or
            @($r.checks | Where-Object { -not $_.passed }).Count -ne 0) {
            throw "Owned core evidence failed for $_`: $output"
        }
        $r
    }
    $hostReport = $reports[0]
    if ($hostReport.authority_bodies.Count -ne 3) { throw "Expected three authoritative body bindings: $output" }
    foreach ($r in $reports) {
        $binding = @($hostReport.authority_bodies | Where-Object { $_.entity -eq $r.entity -and $_.body -eq $r.body -and $_.life -eq $r.life })
        if ($r.generation -ne $hostReport.generation -or $binding.Count -ne 1) {
            throw "Owned projection does not match the authoritative body/life: $output"
        }
    }
    Stop-LDDemoClients -Harness $harness -Message 'Owning clients explicitly leave the room' -ExitError "Core process did not exit cleanly: $output"
    Wait-LDDemoEvidence -Harness $harness -Names @('Disconnects.json')
    $disconnect = Read-LDDemoReport -Harness $harness -Name 'Disconnects.json'
    if (-not $disconnect.passed -or $disconnect.retired_clients -ne 2 -or $disconnect.remaining_live_bodies -ne 1) {
        throw "Explicit room leave did not retire authoritative body/resource ownership: $output"
    }
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness)
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host "DEV-only owned-body core/ground/native CMC scenario passed. Not full gameplay or CT06. Evidence: $output"
}
finally {
    Close-LDDemoHarness -Harness $harness
}
