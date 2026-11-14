# #198: the default covers the opening rest R0 (180 s build + 5 s return, no PK sign-ups) before the field-1 walk.
[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(30,600)][int]$TimeoutSeconds=330, [switch]$EarlyRestReady, [switch]$ReplayFieldReturn, [switch]$FinalEntryOnly,
    [ValidatePattern('^-Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+( -Pkt(Lag|LagVariance|Loss|Dup|Order)=\d+)*$')][string]$NetEmulation)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoCore-OwnedBodies-Packaged' } else { 'DemoCore-OwnedBodies' })
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

function Start-CoreRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCoreProbe -LDDemoCoreRole={2} -LDDemoCoreTimeout={3} -LDDemoCoreOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    if ($EarlyRestReady) { $arguments += ' -LDDemoCoreReady' }
    if ($ReplayFieldReturn) { $arguments += ' -LDDemoCoreReplayFieldReturn' }
    if ($NetEmulation) { $arguments += " $NetEmulation" }
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Wait-CoreReports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Native core process exited before evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Native core evidence timed out: $output"
}

try {
    Start-CoreRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCoreHost'
    Wait-CoreReports @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCoreClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-CoreRole "Client$index" $url
    }
    if ($FinalEntryOnly) {
        Wait-CoreReports @('FinalEntry.json')
        $entry = Get-Content -LiteralPath (Join-Path $evidence 'FinalEntry.json') -Raw | ConvertFrom-Json
        if (-not $entry.passed -or $entry.entries -ne 6) { throw "Final entry preparation failed: $($entry.detail). Evidence: $output" }
        Write-Host "All six final entries prepared through the authoritative spawn path. Evidence: $output"
        return
    }
    Wait-CoreReports @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $r = Get-Content -LiteralPath (Join-Path $evidence "$_.json") -Raw | ConvertFrom-Json
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
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Core process did not exit cleanly: $output" }
    }
    Wait-CoreReports @('Disconnects.json')
    $disconnect = Get-Content -LiteralPath (Join-Path $evidence 'Disconnects.json') -Raw | ConvertFrom-Json
    if (-not $disconnect.passed -or $disconnect.retired_clients -ne 2 -or $disconnect.remaining_live_bodies -ne 1) {
        throw "Disconnect did not retire authoritative body/resource ownership: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host "DEV-only owned-body core/ground/native CMC scenario passed. Not full gameplay or CT06. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
