Set-StrictMode -Version Latest

function Get-LDDemoMaps {
    @('/Game/Lockdown/Generated/Maps/L_Frontend', '/Game/Lockdown/Generated/Maps/L_Demo')
}

function Assert-LDDemoInputs {
    $compiler = Join-Path $PSScriptRoot 'SyncDemoContentData.py'
    if (-not (Test-Path -LiteralPath $compiler)) {
        throw 'Missing Demo content compiler. Legacy catalogs cannot be used for the Demo.'
    }
    & python -B $compiler --check
    if ($LASTEXITCODE -ne 0) {
        throw 'Demo inputs are unavailable or inconsistent. Approved content, the world payload and generated native contracts are required; no legacy fallback is allowed.'
    }
}

function Assert-LDDemoPackage {
    param([Parameter(Mandatory)]$Package)
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $Package.Root '..\..\PackageManifest.json'))
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw 'Missing Demo package manifest. This launcher does not run legacy or unverified packages.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $version = $manifest.PSObject.Properties['demoEntryContractVersion']
    if (-not $version -or $version.Value -ne 1) { throw 'Retired package entry contract. Build and validate the single Demo package before launching it.' }
    $expected = @(Get-LDDemoMaps)
    if (@(Compare-Object -ReferenceObject $expected -DifferenceObject @($manifest.maps)).Count -ne 0) {
        throw 'Package map set does not match the Demo contract.'
    }
    $runtime = @($manifest.files | Where-Object { $_.path.Replace('/', '\') -eq 'Lockdown\Binaries\Win64\Lockdown.exe' })
    if ($runtime.Count -ne 1 -or (Get-FileHash -LiteralPath $Package.Binary -Algorithm SHA256).Hash -ne $runtime[0].sha256) {
        throw 'Packaged executable does not match its Demo manifest.'
    }
}

# One launch description for owned-connection runners: the editor game target, or an archived Development
# package whose manifest matches its executable. Probes accept evidence only below their own project's
# Artifacts, which for a package lies inside the package; Complete-LDGameEvidence moves it back out.
function Get-LDGameLaunch {
    param([string]$EngineRoot, [string]$PackageRoot, [Parameter(Mandatory)][string]$Output)
    if ($PackageRoot) {
        $package = Resolve-LDPackage $PackageRoot
        Assert-LDDemoPackage $package
        $probeRoot = Join-Path $package.Root 'Lockdown\Artifacts'
        $probeOutput = Join-Path $probeRoot (Split-Path $Output -Leaf)
        New-Item -ItemType Directory -Path $probeOutput | Out-Null
        return [pscustomobject]@{ Packaged = $true; Binary = $package.Binary; Project = ''; Package = $package.Root
            Output = $Output; ProbeRoot = $probeRoot; ProbeOutput = $probeOutput }
    }
    $engine = Resolve-LDEngine $EngineRoot
    Assert-LDEditorModules
    Set-LDPlatformValidation Skip
    [pscustomobject]@{ Packaged = $false; Binary = (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')
        Project = (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'); Package = ''; Output = $Output; ProbeRoot = ''; ProbeOutput = $Output }
}

# Editor-only switches stay out of packaged command lines; an empty URL opens the default frontend map.
function Format-LDGameArguments {
    param([Parameter(Mandatory)]$Launch, [string]$Url, [Parameter(Mandatory)][string]$Arguments)
    $target = if ($Url) { '"{0}" ' -f $Url } else { '' }
    if ($Launch.Packaged) { return $target + $Arguments }
    '"{0}" {1}-game -DisablePlugins=AllToolsets,ModelContextProtocol,Fab {2}' -f $Launch.Project, $target, $Arguments
}

# Engine packet simulation (DO_ENABLE_NET_TEST, so Development and editor builds only): each process impairs the
# packets it sends. Proves that every role's net driver parsed each requested setting, so an ignored switch
# cannot pass as an impaired run.
function Assert-LDNetEmulationApplied {
    param([Parameter(Mandatory)][string]$Output, [Parameter(Mandatory)][string[]]$Roles, [string]$NetEmulation)
    if (-not $NetEmulation) { return }
    foreach ($role in $Roles) {
        $log = Join-Path $Output "$role.log"
        foreach ($setting in [regex]::Matches($NetEmulation, '-Pkt(\w+)=(\d+)')) {
            $line = 'Pkt{0} set to {1}' -f $setting.Groups[1].Value, [int]$setting.Groups[2].Value
            if (-not (Select-String -LiteralPath $log -Pattern ('\b{0}\b' -f [regex]::Escape($line)) -Quiet)) {
                throw "$role did not apply packet simulation ($line). Evidence: $Output"
            }
        }
    }
}

# Moves packaged probe evidence next to the runner logs and leaves the package without test output.
function Complete-LDGameEvidence {
    param([Parameter(Mandatory)]$Launch)
    if (-not $Launch.Packaged -or -not (Test-Path -LiteralPath $Launch.ProbeOutput)) { return }
    Get-ChildItem -LiteralPath $Launch.ProbeOutput -Force | Move-Item -Destination $Launch.Output -Force
    Remove-Item -LiteralPath $Launch.ProbeOutput
    if (-not @(Get-ChildItem -LiteralPath $Launch.ProbeRoot -Force).Count) { Remove-Item -LiteralPath $Launch.ProbeRoot }
}

# The first process is the Listen Host. Scenarios own their arguments, assertions and failure wording;
# this harness owns process lifetime and the shared evidence/leave handshake.
function New-LDDemoHarness {
    param([Parameter(Mandatory)]$Launch, [Parameter(Mandatory)][int]$TimeoutSeconds,
        [Parameter(Mandatory)][scriptblock]$ExitError, [Parameter(Mandatory)][scriptblock]$TimeoutError,
        [int]$PollMilliseconds=200)
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    try { $port = $socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
    $root = Get-LDProjectRoot
    $header = Get-Content -LiteralPath (Join-Path $root 'Source\Lockdown\Public\Framework\LDDemoEntry.h') -Raw
    if ($header -notmatch 'inline\s+constexpr\s+uint32\s+WireProtocol\s*=\s*(\d+)\s*;') { throw 'Cannot read WireProtocol from LDDemoEntry.h' }
    [pscustomobject]@{
        Launch = $Launch
        Processes = [Collections.Generic.List[Diagnostics.Process]]::new()
        Port = $port
        Wire = [int]$Matches[1]
        Version = Get-LDProjectVersion
        Digest = (Get-FileHash -LiteralPath (Join-Path $root 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        TimeoutSeconds = $TimeoutSeconds
        PollMilliseconds = $PollMilliseconds
        ExitError = $ExitError
        TimeoutError = $TimeoutError
    }
}

function Start-LDDemoRole {
    param([Parameter(Mandatory)]$Harness, [Parameter(Mandatory)][string]$Role,
        [Parameter(Mandatory)][string]$Url, [Parameter(Mandatory)][scriptblock]$Arguments,
        # A role recorded on screen (capture tools only): its window is shown instead of hidden.
        [switch]$Visible)
    $user = Join-Path $Harness.Launch.Output "$Role-User"
    New-Item -ItemType Directory -Path $user | Out-Null
    $log = Join-Path $Harness.Launch.Output "$Role.log"
    $roleArguments = & $Arguments $user $log
    $formatted = Format-LDGameArguments -Launch $Harness.Launch -Url $Url -Arguments $roleArguments
    $Harness.Processes.Add((Start-Process -FilePath $Harness.Launch.Binary -ArgumentList $formatted -WindowStyle $(if ($Visible) { 'Normal' } else { 'Hidden' }) -PassThru))
}

function Wait-LDDemoEvidence {
    param([Parameter(Mandatory)]$Harness, [Parameter(Mandatory)][string[]]$Names)
    $evidence = $Harness.Launch.ProbeOutput
    $deadline = [DateTime]::UtcNow.AddSeconds($Harness.TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($process in $Harness.Processes) {
            $process.Refresh()
            if ($process.HasExited -and (-not (Test-Path -LiteralPath (Join-Path $evidence 'FinishClients')) -or
                $process.Id -eq $Harness.Processes[0].Id -or $process.ExitCode -ne 0)) {
                throw (& $Harness.ExitError $process)
            }
        }
        if (@($Names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $evidence $_)) }).Count -eq 0) { return }
        Start-Sleep -Milliseconds $Harness.PollMilliseconds
    }
    throw (& $Harness.TimeoutError)
}

function Read-LDDemoReport {
    param([Parameter(Mandatory)]$Harness, [Parameter(Mandatory)][string]$Name)
    Get-Content -LiteralPath (Join-Path $Harness.Launch.ProbeOutput $Name) -Raw | ConvertFrom-Json
}

function Stop-LDDemoClients {
    param([Parameter(Mandatory)]$Harness, [Parameter(Mandatory)][string]$Message,
        [Parameter(Mandatory)][string]$ExitError, [int[]]$GraceMilliseconds=@(30000))
    Set-Content -LiteralPath (Join-Path $Harness.Launch.ProbeOutput 'FinishClients') -Value $Message
    for ($index = 1; $index -lt $Harness.Processes.Count; $index++) {
        $process = $Harness.Processes[$index]
        $grace = $GraceMilliseconds[[Math]::Min($index - 1, $GraceMilliseconds.Length - 1)]
        if (-not $process.WaitForExit($grace) -or $process.ExitCode -ne 0) { throw $ExitError }
    }
}

function Stop-LDDemoHost {
    param([Parameter(Mandatory)]$Harness)
    Set-Content -LiteralPath (Join-Path $Harness.Launch.ProbeOutput 'Finish') -Value 'Complete'
    $hostProcess = $Harness.Processes[0]
    if (-not $hostProcess.WaitForExit(30000) -or $hostProcess.ExitCode -ne 0) { throw "Host did not exit cleanly: $($Harness.Launch.Output)" }
}

function Get-LDDemoLogErrors {
    param([Parameter(Mandatory)]$Harness,
        [string]$Pattern='Fatal error|Assertion failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT')
    Get-ChildItem -LiteralPath $Harness.Launch.Output -Filter '*.log' -File | Select-String -Pattern $Pattern
}

function Close-LDDemoHarness {
    param([Parameter(Mandatory)]$Harness)
    foreach ($process in $Harness.Processes) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    Complete-LDGameEvidence $Harness.Launch
}
