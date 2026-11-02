[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(30,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'DemoWorld-OwnedRoom'
$socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
$port = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$digest = (Get-FileHash -LiteralPath (Join-Path (Get-LDProjectRoot) 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$version = Get-LDProjectVersion
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$binary = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Start-Role {
    param([string]$Role, [string]$Url)
    $user = Join-Path $output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
    $arguments = '"{0}" "{1}" -game -unattended -nop4 -nosound -nullrhi -multihome=127.0.0.1 {2} -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{3}" -LDDemoWorldRoomProbe -LDDemoWorldRoomRole={4} -LDDemoWorldRoomTimeout={5} -LDDemoWorldRoomOutput="{6}" -abslog="{7}"' -f $project,$Url,$portArgument,$user,$Role,$TimeoutSeconds,$output,(Join-Path $output "$Role.log")
    $processes.Add((Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru))
}

function Wait-Reports {
    param([string[]]$Names)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $processes) {
            $process.Refresh()
            if ($process.HasExited) { throw "Native room process exited before all participants finished: PID $($process.Id). Evidence: $output" }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $output $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Native owned-room evidence timed out: $output"
}

try {
    Start-Role 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoWorldHost'
    Wait-Reports @('HostReady.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoWorldClient{1}?LDWire=2?LDProduct={2}?LDCatalog={3}' -f $port,$index,$version,$digest
        Start-Role "Client$index" $url
    }
    Wait-Reports @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $report = Get-Content -LiteralPath (Join-Path $output "$_.json") -Raw | ConvertFrom-Json
        if (-not $report.passed -or $report.role -ne $_ -or $report.catalog_sha256 -ne $digest -or $report.pawns -ne 0 -or
            @($report.checks | Where-Object { -not $_.passed }).Count -ne 0 -or $report.contenders.Count -ne 3) {
            throw "Native room evidence failed for $_`: $output"
        }
        $report
    }
    $generation = $reports[0].generation
    $members = @($reports[0].contenders | Sort-Object) -join ','
    foreach ($report in $reports) {
        if ($report.generation -ne $generation -or (@($report.contenders | Sort-Object) -join ',') -ne $members) {
            throw "Owned connections disagree on the authoritative roster/generation: $output"
        }
    }
    Set-Content -LiteralPath (Join-Path $output 'Finish') -Value 'Complete'
    foreach ($process in $processes) {
        if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Owned-room process did not exit cleanly: $output" }
    }
    Write-Host "Three real human Lobby connections share the saved Demo world and approved navigation. Evidence: $output"
}
finally {
    foreach ($process in $processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
}
