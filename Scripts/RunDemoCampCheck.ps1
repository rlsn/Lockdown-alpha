[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(120,900)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoCamp-OwnedMatch-Packaged' } else { 'DemoCamp-OwnedMatch' })
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
# Each role must have reported every check it owns; a missing id is a failure, not a pass.
$requiredChecks = @{
    Host = @('#C1','#C2','#C3','#C4','#C5','#C6','#C7')
    Client1 = @('#C1','#C3','#C4','#C5','#C7')
    Client2 = @('#C1','#C6')
}

function Start-CampRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '-unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCampProbe -LDDemoCampRole={2} -LDDemoCampTimeout={3} -LDDemoCampOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $Url -Arguments $arguments) -WindowStyle Hidden -PassThru))
}

function Get-CampFailure {
    foreach ($role in 'Host','Client1','Client2') {
        $file = Join-Path $evidence "$role.json"
        if (Test-Path -LiteralPath $file) {
            $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (-not $report.passed) { return "$role reported: $($report.detail)" }
        }
    }
    return 'no role report explains it'
}

function Wait-CampReports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Camp scenario process exited before evidence completed: PID $($process.Id); $(Get-CampFailure). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Camp scenario evidence timed out: $output"
}

try {
    Start-CampRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCampHost'
    Wait-CampReports @('HostJoined.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCampClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-CampRole "Client$index" $url
    }
    Wait-CampReports @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $role = $_
        $r = Get-Content -LiteralPath (Join-Path $evidence "$role.json") -Raw | ConvertFrom-Json
        if (-not $r.passed -or $r.role -ne $role -or @($r.checks | Where-Object { -not $_.passed }).Count -ne 0) {
            throw "Owned camp evidence failed for $role`: $($r.detail). Evidence: $output"
        }
        $missing = @($requiredChecks[$role] | Where-Object { $id = $_; -not @($r.checks | Where-Object { $_.id -eq $id }).Count })
        if ($missing.Count) { throw "Owned camp evidence for $role lacks $($missing -join ', '). Evidence: $output" }
        $r
    }
    if (@($reports | Where-Object { $_.generation -ne $reports[0].generation }).Count) {
        throw "Roles disagree on the match generation: $output"
    }
    Set-Content -LiteralPath (Join-Path $evidence 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Camp client did not exit cleanly: $output" }
    }
    Set-Content -LiteralPath (Join-Path $evidence 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Camp scenario logged an error: $($errors[0].Line). Evidence: $output" }
    Write-Host "DEV-only normal-start camp scenario passed on real owned connections. Not 3+3, AI, capture or balance acceptance. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $launch
}
