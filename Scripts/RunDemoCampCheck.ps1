[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot, [ValidateRange(120,900)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory $(if ($PackageRoot) { 'DemoCamp-OwnedMatch-Packaged' } else { 'DemoCamp-OwnedMatch' })
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds ($TimeoutSeconds + 60) -PollMilliseconds 200 `
    -ExitError { param($process) "Camp scenario process exited before evidence completed: PID $($process.Id); $(Get-CampFailure). Evidence: $output" } `
    -TimeoutError { "Camp scenario evidence timed out: $output" }
$evidence = $launch.ProbeOutput
$port = $harness.Port
$digest = $harness.Digest
$version = $harness.Version
$wire = $harness.Wire
# Each role must have reported every check it owns; a missing id is a failure, not a pass.
$requiredChecks = @{
    Host = @('#C1','#C2','#C3','#C4','#C5','#C6','#C7','#C11','#C12')
    Client1 = @('#C1','#C3','#C4','#C5','#C7','#C10','#C13')
    Client2 = @('#C1','#C6','#C9')
}
# Kept by the rendered Client2 for presentation review only: required as PNG files, never pixel-compared.
$images = @('CampGuardianReturning.png','CampLowPair.png')

function Start-CampRole {
    param([string]$Role,[string]$Url)
    Start-LDDemoRole -Harness $harness -Role $Role -Url $Url -Arguments {
        param($user, $log)
        $portArgument = if ($Role -eq 'Host') { "-port=$port" } else { '' }
        # Client2 is the one rendered owner and takes the screenshots; the other roles need no renderer.
        $render = if ($Role -eq 'Client2') { '-windowed -RenderOffScreen -ResX=1280 -ResY=720' } else { '-nullrhi' }
        $arguments = '-unattended -nop4 -nosound {6} -multihome=127.0.0.1 {0} -UserDir="{1}" -LDDemoCampProbe -LDDemoCampRole={2} -LDDemoCampTimeout={3} -LDDemoCampOutput="{4}" -abslog="{5}"' -f $portArgument,$user,$Role,$TimeoutSeconds,$evidence,$log,$render
        $arguments
    }
}

function Test-CampPng {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $stream = [IO.File]::OpenRead($Path)
    try { $head = [byte[]]::new(8); $read = $stream.Read($head, 0, 8) } finally { $stream.Dispose() }
    return $read -eq 8 -and $head[0] -eq 0x89 -and $head[1] -eq 0x50 -and $head[2] -eq 0x4E -and $head[3] -eq 0x47
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

try {
    Start-CampRole 'Host' '/Game/Lockdown/Generated/Maps/L_Demo?listen?Name=DemoCampHost'
    Wait-LDDemoEvidence -Harness $harness -Names @('HostJoined.json')
    foreach ($index in 1..2) {
        $url = '127.0.0.1:{0}?Name=DemoCampClient{1}?LDWire={2}?LDProduct={3}?LDCatalog={4}' -f $port,$index,$wire,$version,$digest
        Start-CampRole "Client$index" $url
    }
    Wait-LDDemoEvidence -Harness $harness -Names @('Host.json','Client1.json','Client2.json')
    $reports = @('Host','Client1','Client2') | ForEach-Object {
        $role = $_
        $r = Read-LDDemoReport -Harness $harness -Name "$role.json"
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
    foreach ($image in $images) {
        if (@($reports[2].screenshots) -notcontains $image -or -not (Test-CampPng (Join-Path $evidence $image))) {
            throw "Client2 kept no PNG screenshot $image. Evidence: $output"
        }
    }
    Stop-LDDemoClients -Harness $harness -Message 'Disconnect owning clients' -ExitError "Camp client did not exit cleanly: $output" -GraceMilliseconds @(30000,120000)
    Stop-LDDemoHost -Harness $harness
    $errors = @(Get-LDDemoLogErrors -Harness $harness)
    if ($errors.Count) { throw "Camp scenario logged an error: $($errors[0].Line). Evidence: $output" }
    $shots = ($images | ForEach-Object { Join-Path $output $_ }) -join '; '
    Write-Host "DEV-only normal-start camp scenario passed on real owned connections. Not 3+3, AI, capture or balance acceptance. Screenshots for presentation review (not pixel-compared): $shots. Evidence: $output"
}
finally {
    Close-LDDemoHarness -Harness $harness
}
