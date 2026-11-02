[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(30,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'DemoCore-OwnedBodies'
$socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
$port = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$digest = (Get-FileHash -LiteralPath (Join-Path (Get-LDProjectRoot) 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$version = Get-LDProjectVersion
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$binary = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Start-CoreRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '"{0}" "{1}" -game -unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {2} -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{3}" -LDDemoCoreProbe -LDDemoCoreRole={4} -LDDemoCoreTimeout={5} -LDDemoCoreOutput="{6}" -abslog="{7}"' -f $project,$Url,$portArgument,$user,$Role,$TimeoutSeconds,$output,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru))
}

function Wait-CoreReports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $output 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Native core process exited before evidence completed: PID $($process.Id). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $output $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Native core evidence timed out: $output"
}

try {
    Start-CoreRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCoreHost'
    Wait-CoreReports @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCoreClient{1}?LDWire=3?LDProduct={2}?LDCatalog={3}' -f $port,$index,$version,$digest
        Start-CoreRole "Client$index" $url
    }
    Wait-CoreReports @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $r = Get-Content -LiteralPath (Join-Path $output "$_.json") -Raw | ConvertFrom-Json
        if (-not $r.passed -or $r.role -ne $_ -or -not $r.fixture_only -or $r.full_ct06 -or $r.catalog_sha256 -ne $digest -or
            $r.life -ne 1 -or @($r.checks | Where-Object { -not $_.passed }).Count -ne 0) {
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
    Set-Content -LiteralPath (Join-Path $output 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Core process did not exit cleanly: $output" }
    }
    Wait-CoreReports @('Disconnects.json')
    $disconnect = Get-Content -LiteralPath (Join-Path $output 'Disconnects.json') -Raw | ConvertFrom-Json
    if (-not $disconnect.passed -or $disconnect.retired_clients -ne 2 -or $disconnect.remaining_live_bodies -ne 1) {
        throw "Disconnect did not retire authoritative body/resource ownership: $output"
    }
    Set-Content -LiteralPath (Join-Path $output 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Native scenario logged an error, including after its success reports: $($errors[0].Line). Evidence: $output" }
    Write-Host "DEV-only owned-body core/ground/native CMC scenario passed. Not full gameplay or CT06. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
}
