[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
$output = New-LDArtifactDirectory 'DemoEntry-ScriptTests'
$checks = [Collections.Generic.List[string]]::new()

function Assert-Case {
    param([bool]$Passed, [string]$Name)
    if (-not $Passed) { throw "Demo entry check failed: $Name" }
    $checks.Add($Name)
}

foreach ($entry in @(
    @{ File = 'OpenStandalone.ps1'; Retired = @('Profile', 'NavigationCheckpoint') },
    @{ File = 'OpenPackaged.ps1'; Retired = @('Profile') },
    @{ File = 'BootstrapAssets.ps1'; Retired = @('RebuildRegionMaps', 'RebuildV08Region') },
    @{ File = 'RunDemoEntryCheck.ps1'; Retired = @('Profile') },
    @{ File = 'RunDemoFrontendCheck.ps1'; Retired = @('Profile') },
    @{ File = 'PackageWin64.ps1'; Retired = @('Profile', 'Map') }
)) {
    $path = Join-Path $PSScriptRoot $entry.File
    $tokens = $null
    $errors = $null
    $tree = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    Assert-Case ($errors.Count -eq 0) "$($entry.File) parses"
    $parameters = @($tree.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
    foreach ($name in $entry.Retired) {
        Assert-Case ($name -notin $parameters) "$($entry.File) no longer exposes $name"
        $arguments = @{ $name = 'retired' }
        if ($entry.File -eq 'OpenPackaged.ps1') { $arguments.PackageRoot = 'not-opened' }
        $rejected = $false
        try { & $path @arguments }
        catch { $rejected = $_.FullyQualifiedErrorId -like 'NamedParameterNotFound*' }
        Assert-Case $rejected "$($entry.File) rejects $name before executing"
    }
}
$maps = @(Get-LDDemoMaps)
Assert-Case ($maps.Count -eq 2 -and $maps[0] -eq '/Game/Lockdown/Generated/Maps/L_Frontend' -and $maps[1] -eq '/Game/Lockdown/Generated/Maps/L_Demo') 'Only frontend and Demo packages are selected'

$fixture = Join-Path $output 'PackageFixture'
$packageRoot = Join-Path $fixture 'Archive\Windows'
$binary = Join-Path $packageRoot 'Lockdown\Binaries\Win64\Lockdown.exe'
$manifestPath = Join-Path $fixture 'PackageManifest.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $binary) -Force | Out-Null
try {
    [IO.File]::WriteAllText($binary, 'Non-executable isolated manifest fixture')
    $package = [pscustomobject]@{ Root = $packageRoot; Binary = $binary }
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Missing Demo package manifest*' }
    Assert-Case $rejected 'Package without a Demo manifest is rejected'
    $manifest = @{
        demoEntryContractVersion = 1
        maps = $maps
        files = @(@{ path = 'Lockdown\Binaries\Win64\Lockdown.exe'; sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash })
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Assert-LDDemoPackage $package
    $checks.Add('Exact isolated manifest and binary bytes match; no executable is launched')
    $manifest.demoEntryContractVersion = 0
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Retired package entry contract*' }
    Assert-Case $rejected 'Old package contract is rejected'
    $manifest.demoEntryContractVersion = 1
    $manifest.maps = @($maps[0], '/Game/Lockdown/Generated/Maps/L_DevelopmentRegion')
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Package map set*' }
    Assert-Case $rejected 'Legacy package map is rejected'
    $manifest.maps = $maps
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    [IO.File]::AppendAllText($binary, ' changed')
    $rejected = $false
    try { Assert-LDDemoPackage $package } catch { $rejected = $_.Exception.Message -like 'Packaged executable does not match*' }
    Assert-Case $rejected 'Stale packaged binary is rejected'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

$fixture = Join-Path $output 'HarnessFixture'
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    & {
        $launch = [pscustomobject]@{ Output = $fixture; ProbeOutput = $fixture; Packaged = $false
            Binary = 'NotLaunched.exe'; Project = 'C:\Project With Spaces\Lockdown.uproject' }
        $harness = New-LDDemoHarness -Launch $launch -TimeoutSeconds 1 -PollMilliseconds 1 `
            -ExitError { param($process) "Early exit: $($process.Id)" } -TimeoutError { 'Evidence timeout' }
        $launches = [Collections.Generic.List[object]]::new()
        function Start-Process {
            param($FilePath, $ArgumentList, $WindowStyle, [switch]$PassThru)
            $launches.Add(@{ Binary = $FilePath; Arguments = $ArgumentList })
            [Diagnostics.Process]::GetCurrentProcess()
        }
        foreach ($packaged in @($false, $true)) {
            $launch.Packaged = $packaged
            $role = if ($packaged) { 'Client1' } else { 'Host' }
            Start-LDDemoRole -Harness $harness -Role $role -Url '127.0.0.1:1234?Name=Owner' -Arguments {
                param($user, $log)
                '-nullrhi -UserDir="{0}" -abslog="{1}"' -f $user, $log
            }
            $prefix = if ($packaged) { '' } else { '"C:\Project With Spaces\Lockdown.uproject" ' }
            $editor = if ($packaged) { '' } else { '-game -DisablePlugins=AllToolsets,ModelContextProtocol,Fab ' }
            $expected = $prefix + '"127.0.0.1:1234?Name=Owner" ' + $editor +
                ('-nullrhi -UserDir="{0}" -abslog="{1}"' -f (Join-Path $fixture "$role-User"), (Join-Path $fixture "$role.log"))
            Assert-Case ($launches[-1].Arguments -ceq $expected -and (Test-Path -LiteralPath (Join-Path $fixture "$role-User"))) "Harness preserves launch quoting and role paths (packaged=$packaged)"
        }
        Assert-Case ($harness.Processes.Count -eq 2) 'Harness tracks Host before owning clients'
        $launch.Packaged = $false
        $polls = [Collections.Generic.List[int]]::new()
        function Start-Sleep {
            param($Milliseconds)
            $polls.Add($Milliseconds)
            '{"passed":true,"generation":7}' | Set-Content -LiteralPath (Join-Path $fixture 'Ready.json')
        }
        Wait-LDDemoEvidence -Harness $harness -Names @('Ready.json')
        $report = Read-LDDemoReport -Harness $harness -Name 'Ready.json'
        Assert-Case ($polls.Count -eq 1 -and $report.passed -and $report.generation -eq 7) 'Harness polls for complete evidence and reads the report'
        $harness.TimeoutSeconds = 0
        $rejected = $false
        try { Wait-LDDemoEvidence -Harness $harness -Names @('Missing.json') }
        catch { $rejected = $_.Exception.Message -eq 'Evidence timeout' }
        Assert-Case $rejected 'Missing evidence uses the scenario timeout error'
        $harness.TimeoutSeconds = 1
        foreach ($code in @(0, 3)) {
            $child = Microsoft.PowerShell.Management\Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList "-NoProfile -Command exit $code" -WindowStyle Hidden -PassThru
            try {
                Assert-Case ($child.WaitForExit(30000)) "Harmless exit-$code process fixture terminates"
                $harness.Processes.Clear()
                $harness.Processes.Add([Diagnostics.Process]::GetCurrentProcess())
                $harness.Processes.Add($child)
                $rejected = $false
                try { Wait-LDDemoEvidence -Harness $harness -Names @('Ready.json') }
                catch { $rejected = $_.Exception.Message -eq "Early exit: $($child.Id)" }
                Assert-Case $rejected "Exited client ($code) cannot pass before FinishClients"
                'Leave room' | Set-Content -LiteralPath (Join-Path $fixture 'FinishClients')
                $rejected = $false
                try { Wait-LDDemoEvidence -Harness $harness -Names @('Ready.json') }
                catch { $rejected = $_.Exception.Message -eq "Early exit: $($child.Id)" }
                Assert-Case ($rejected -eq ($code -ne 0)) "Only a clean departed client is allowed after FinishClients ($code)"
                $harness.Processes.Clear()
                $harness.Processes.Add($child)
                $rejected = $false
                try { Wait-LDDemoEvidence -Harness $harness -Names @('Ready.json') }
                catch { $rejected = $_.Exception.Message -eq "Early exit: $($child.Id)" }
                Assert-Case $rejected "Exited Host ($code) remains a failure after FinishClients"
            } finally {
                if (-not $child.HasExited) { Microsoft.PowerShell.Management\Stop-Process -Id $child.Id }
                $child.Dispose()
                Remove-Item -LiteralPath (Join-Path $fixture 'FinishClients') -ErrorAction SilentlyContinue
            }
        }
        @('LogPython: Error: warning only', 'Ensure condition failed: assertion',
            'LogLDVision: Display: LD_OBSERVATION_PROTOCOL_REJECTED', 'LogLDCombat: Error: fault') |
            Set-Content -LiteralPath (Join-Path $fixture 'Host.log')
        $normal = @(Get-LDDemoLogErrors -Harness $harness)
        $vision = @(Get-LDDemoLogErrors -Harness $harness -Pattern 'Fatal error|Assertion failed|Ensure condition failed|Log(Temp|LD\w+): Error:|LogNet: Error:|LD_DEMO_AUTHORITY_FAULT|LD_OBSERVATION_PROTOCOL_REJECTED')
        Assert-Case ($normal.Count -eq 1 -and $vision.Count -eq 3) 'Log scanning preserves scenario-specific errors without promoting Python warnings'
    }
    & {
        $events = [Collections.Generic.List[string]]::new()
        $launch = [pscustomobject]@{ Output = $fixture; ProbeOutput = $fixture; Packaged = $false }
        $processes = foreach ($role in @('Host', 'Client1', 'Client2')) {
            $process = [pscustomobject]@{ Role = $role; Evidence = $fixture; Events = $events; ExitCode = 0; HasExited = $false }
            $process | Add-Member ScriptMethod WaitForExit {
                param($Milliseconds)
                $this.Events.Add(('{0}:{1}:{2}:{3}' -f $this.Role, $Milliseconds,
                    (Test-Path -LiteralPath (Join-Path $this.Evidence 'FinishClients')),
                    (Test-Path -LiteralPath (Join-Path $this.Evidence 'Finish'))))
                $this.HasExited = $true
                return $true
            }
            $process
        }
        $harness = [pscustomobject]@{ Launch = $launch; Processes = $processes }
        Stop-LDDemoClients -Harness $harness -Message 'Leave room' -ExitError 'Client failed' -GraceMilliseconds @(30000,120000)
        Assert-Case ((Get-Content -LiteralPath (Join-Path $fixture 'FinishClients') -Raw).Trim() -eq 'Leave room' -and
            -not (Test-Path -LiteralPath (Join-Path $fixture 'Finish'))) 'Client leave completes before Host finish; disconnect assertions can run between them'
        Stop-LDDemoHost -Harness $harness
        Assert-Case (($events -join '|') -ceq 'Client1:30000:True:False|Client2:120000:True:False|Host:30000:True:True') 'Shutdown preserves role order and rendered-client grace'
        $processes[1].ExitCode = 3
        $rejected = $false
        try { Stop-LDDemoClients -Harness $harness -Message 'Leave room' -ExitError 'Client failed' }
        catch { $rejected = $_.Exception.Message -eq 'Client failed' }
        Assert-Case $rejected 'Nonzero client exit retains the scenario failure'
    }
    & {
        $probeRoot = Join-Path $fixture 'PackageArtifacts'
        $probe = Join-Path $probeRoot 'Run'
        $destination = Join-Path $fixture 'Collected'
        New-Item -ItemType Directory -Path $probe, $destination | Out-Null
        'report' | Set-Content -LiteralPath (Join-Path $probe 'Host.json')
        $stopped = [Collections.Generic.List[int]]::new()
        function Stop-Process { param([int]$Id) $stopped.Add($Id) }
        $process = [pscustomobject]@{ Id = 123; HasExited = $false }
        $process | Add-Member ScriptMethod Refresh {}
        $launch = [pscustomobject]@{ Packaged = $true; Output = $destination; ProbeRoot = $probeRoot; ProbeOutput = $probe }
        Close-LDDemoHarness -Harness ([pscustomobject]@{ Launch = $launch; Processes = @($process) })
        Assert-Case ($stopped.Count -eq 1 -and $stopped[0] -eq 123 -and
            (Test-Path -LiteralPath (Join-Path $destination 'Host.json')) -and -not (Test-Path -LiteralPath $probeRoot)) 'Cleanup stops only owned processes and collects packaged evidence'
    }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
$fixture = Join-Path $output 'EnvironmentFixture'
$engineOverride = $env:LD_UE_ROOT
New-Item -ItemType Directory -Path (Join-Path $fixture 'Config\Local') -Force | Out-Null
try {
    & {
        function Get-LDProjectRoot { $fixture }
        $lock = @{ engine = @{ major = 5; minor = 8; patch = 4; changelist = 58902052 } }
        $lock | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'Config\PlatformLock.json')
        @{ EngineAssociation = 'IsolatedEnvironmentFixture' } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $fixture 'Lockdown.uproject')
        $engines = foreach ($name in @('Engine #1', 'Engine Two')) {
            $engine = Join-Path $fixture $name
            New-Item -ItemType Directory -Path (Join-Path $engine 'Engine\Build') -Force | Out-Null
            @{ MajorVersion = 5; MinorVersion = 8; PatchVersion = 4; Changelist = 58902052 } |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $engine 'Engine\Build\Build.version')
            $engine
        }
        $env:LD_UE_ROOT = ''
        $dotenv = Join-Path $fixture '.env'
        foreach ($quote in @('', '"', "'")) {
            @('# Local engine', 'UNRELATED_SETTING=unused', "LD_UE_ROOT=$quote$($engines[0])$quote") |
                Set-Content -LiteralPath $dotenv
            Assert-Case ((Resolve-LDEngine) -eq $engines[0]) "Engine .env reads literal spaces and hash characters (quote=$quote)"
        }
        $env:LD_UE_ROOT = $engines[1]
        Assert-Case ((Resolve-LDEngine) -eq $engines[1]) 'Process engine override takes precedence over .env'
        Assert-Case ((Resolve-LDEngine -EngineRoot $engines[0]) -eq $engines[0]) 'Explicit engine argument takes precedence over environment'
        $env:LD_UE_ROOT = ''
        @{ engineRoot = $engines[1] } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $fixture 'Config\Local\Environment.json')
        Assert-Case ((Resolve-LDEngine) -eq $engines[0]) '.env takes precedence over the existing local JSON fallback'
        Remove-Item -LiteralPath $dotenv
        Assert-Case ((Resolve-LDEngine) -eq $engines[1]) 'Missing .env preserves local JSON fallback'
        foreach ($entry in @("LD_UE_ROOT=`"$($engines[0])", "LD_UE_ROOT=$($engines[0])`nLD_UE_ROOT=$($engines[1])")) {
            $entry | Set-Content -LiteralPath $dotenv
            $rejected = $false
            try { Resolve-LDEngine | Out-Null }
            catch { $rejected = $_.Exception.Message -like 'Invalid LD_UE_ROOT in .env:*' }
            Assert-Case $rejected 'Malformed or duplicate .env engine binding is reported instead of falling back'
        }
        "LD_UE_ROOT=$($engines[0])" | Set-Content -LiteralPath $dotenv
        @{ MajorVersion = 5; MinorVersion = 8; PatchVersion = 3; Changelist = 58902052 } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $engines[0] 'Engine\Build\Build.version')
        $rejected = $false
        try { Resolve-LDEngine | Out-Null }
        catch { $rejected = $_.Exception.Message -like 'Engine does not match pinned*' }
        Assert-Case $rejected '.env cannot bypass the pinned engine version'
    }
} finally {
    $env:LD_UE_ROOT = $engineOverride
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
@{ passed = $true; checks = @($checks) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'EntryChecks.json') -Encoding utf8
Write-Host "Demo entry script checks passed: $($checks.Count). Evidence: $output"
