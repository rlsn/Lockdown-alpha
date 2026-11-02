[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(120,900)][int]$TimeoutSeconds=480)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'DemoCapture-OwnedMatch'
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
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$binary = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
# Each role reports exactly these stable check ids, once each and passed. A missing, extra or repeated id means
# the probe and this runner drifted apart; T01/P01 only appear on failure paths and therefore always fail.
$requiredChecks = @{
    Host = @('C01','C02','C03','C04','C05','C06','C07','C08','C09','C10','C11','C12','R01','R02','R03','R04','R05','R06','R07','R08','R09')
    Client1 = @('C01','C03','C04','C05','C06','C07','C08','C09','C11','C12','R02','R03','R04','R05','R06')
}
$requiredChecks.Client2 = $requiredChecks.Client1

function Start-CaptureRole {
    param([string]$Role,[string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '"{0}" "{1}" -game -unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {2} -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{3}" -LDDemoCaptureProbe -LDDemoCaptureRole={4} -LDDemoCaptureTimeout={5} -LDDemoCaptureOutput="{6}" -abslog="{7}"' -f $project,$Url,$portArgument,$user,$Role,$TimeoutSeconds,$output,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru))
}

function Get-CaptureFailure {
    foreach ($role in 'Host','Client1','Client2') {
        $file = Join-Path $output "$role.json"
        if (Test-Path -LiteralPath $file) {
            $report = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (-not $report.passed) { return "$role reported: $($report.detail)" }
        }
    }
    return 'no role report explains it'
}

function Wait-CaptureReports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 60)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $output 'FinishClients')) -or
                $process.Id -eq $processes[0].Id -or $process.ExitCode -ne 0)) {
                throw "Capture scenario process exited before evidence completed: PID $($process.Id); $(Get-CaptureFailure). Evidence: $output"
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $output $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Capture scenario evidence timed out: $output"
}

try {
    Start-CaptureRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCaptureHost'
    Wait-CaptureReports @('HostJoined.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCaptureClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-CaptureRole "Client$index" $url
    }
    Wait-CaptureReports @('Host.json','Client1.json','Client2.json')
    $reports = foreach ($role in 'Host','Client1','Client2') {
        $r = Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw | ConvertFrom-Json
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
    $winner = $reports[0].summary.winner
    foreach ($r in $reports) {
        if ($r.summary.winner -ne $winner -or $r.summary.ended_at_ms -ne $reports[0].summary.ended_at_ms -or
            $r.summary.next_generation -ne $reports[0].summary.next_generation -or
            -not $r.summary.second_generation -or $r.summary.second_generation -ne $reports[0].summary.second_generation) {
            throw "Roles disagree on the unique result, the next Lobby generation or the second match generation: $output"
        }
    }
    Set-Content -LiteralPath (Join-Path $output 'FinishClients') -Value 'Disconnect owning clients'
    foreach ($process in @($processes)[1..2]) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Capture client did not exit cleanly: $output" }
    }
    Set-Content -LiteralPath (Join-Path $output 'Finish') -Value 'Complete'
    if (-not $processes[0].WaitForExit(30000) -or $processes[0].ExitCode -ne 0) { throw "Host did not exit cleanly: $output" }
    $errors = @(Get-ChildItem -LiteralPath $output -Filter '*.log' -File |
        Select-String -Pattern 'Fatal error|Assertion failed|LogTemp: Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    if ($errors.Count) { throw "Capture scenario logged an error: $($errors[0].Line). Evidence: $output" }
    Write-Host ("DEV-only normal-start capture scenario passed on real owned connections, including a second match in generation {0} with fresh contender and camp state ({1}/{2}/{3} required checks). Not 3+3, AI or full gameplay. Evidence: {4}" -f
        $reports[0].summary.second_generation,$requiredChecks.Host.Count,$requiredChecks.Client1.Count,$requiredChecks.Client2.Count,$output)
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
}
