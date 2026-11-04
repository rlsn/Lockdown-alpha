# Manages this machine's self-hosted GitHub Actions runner for the PR check and the nightly playtest package.
# One runner runs one job at a time, and Start refuses while any other runner runs on this machine, so CI never runs
# more than one job here at once; other runs wait in GitHub's queue.
# The runner is never installed as a service: it only takes jobs between Start and Stop. Jobs queued while it is
# off wait on GitHub (up to 24 hours) and run at the next Start.
#   .\Scripts\CIRunner.ps1 Install -RunnerRoot D:\ldci   # once per machine
#   .\Scripts\CIRunner.ps1 Start | Stop | Status
#   .\Scripts\CIRunner.ps1 Uninstall                    # unregisters; the folder is left for you to delete
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)][ValidateSet('Install', 'Start', 'Stop', 'Status', 'Uninstall')][string]$Action,
    [string]$RunnerRoot,
    [string]$Repository,
    [string]$EngineRoot,
    [string]$RunnerName = ('{0}-lockdown' -f $env:COMPUTERNAME.ToLowerInvariant())
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$runnerLabel = 'lockdown-ue58'
$localSettingsPath = Join-Path (Get-LDProjectRoot) 'Config/Local/Environment.json'

function Get-LocalSettings {
    if (Test-Path -LiteralPath $localSettingsPath) { return Get-Content -LiteralPath $localSettingsPath -Raw | ConvertFrom-Json }
    [pscustomobject]@{}
}

function Resolve-RunnerRoot {
    param([switch]$ForInstall)
    $root = $RunnerRoot
    if (-not $root) { $root = $env:LD_CI_RUNNER_ROOT }
    if (-not $root) { $root = (Get-LocalSettings).PSObject.Properties['ciRunnerRoot'].Value }
    if (-not $root) { throw 'Runner folder unknown. Pass -RunnerRoot, set LD_CI_RUNNER_ROOT or ciRunnerRoot in Config/Local/Environment.json.' }
    $root = [IO.Path]::GetFullPath($root)
    if (-not $ForInstall -and -not (Test-Path -LiteralPath (Join-Path $root '.runner'))) {
        throw "No configured runner in $root. Run: .\Scripts\CIRunner.ps1 Install -RunnerRoot <folder>"
    }
    $root
}

function Resolve-Repository {
    if ($Repository) { return $Repository }
    $url = & git -C (Get-LDProjectRoot) remote get-url origin
    if ($url -notmatch 'github\.com[:/](?<repo>[^/]+/[^/]+?)(\.git)?$') { throw "origin is not a GitHub repository: $url" }
    $Matches.repo
}

function Get-RunnerProcesses {
    param([Parameter(Mandatory)][string]$Root)
    $all = @(Get-CimInstance Win32_Process)
    $prefix = $Root.TrimEnd('\') + '\'
    $seeds = @($all | Where-Object {
        ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) -or
        ($_.Name -eq 'cmd.exe' -and $_.CommandLine -and $_.CommandLine.IndexOf($prefix, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    })
    # Include every descendant (build tools, editors, packagers a job started).
    $found = @{}
    $queue = [Collections.Generic.Queue[object]]::new()
    foreach ($seed in $seeds) { $queue.Enqueue($seed) }
    while ($queue.Count) {
        $process = $queue.Dequeue()
        if ($found.ContainsKey($process.ProcessId)) { continue }
        $found[$process.ProcessId] = $process
        foreach ($child in $all | Where-Object { $_.ParentProcessId -eq $process.ProcessId }) { $queue.Enqueue($child) }
    }
    @($found.Values)
}

function Get-RemoteRunner {
    param([Parameter(Mandatory)][string]$Repo)
    $runners = & gh api "repos/$Repo/actions/runners" --jq '.runners' | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw "gh api failed to list runners for $Repo." }
    @($runners | Where-Object { $_.name -eq $RunnerName })[0]
}

function Set-DotEnv {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][hashtable]$Values)
    $lines = [Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path) {
            $key = ($line -split '=', 2)[0]
            if (-not $Values.ContainsKey($key)) { $lines.Add($line) }
        }
    }
    foreach ($key in $Values.Keys | Sort-Object) { $lines.Add("$key=$($Values[$key])") }
    Set-Content -LiteralPath $Path -Value $lines -Encoding ascii
}

switch ($Action) {
    'Install' {
        $root = Resolve-RunnerRoot -ForInstall
        $repo = Resolve-Repository
        $engine = Resolve-LDEngine $EngineRoot
        if (Test-Path -LiteralPath (Join-Path $root '.runner')) { throw "A runner is already configured in $root. Use Uninstall first." }
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $root 'config.cmd'))) {
            $release = & gh api repos/actions/runner/releases/latest | ConvertFrom-Json
            if ($LASTEXITCODE -ne 0) { throw 'Could not read the latest actions/runner release.' }
            $version = $release.tag_name.TrimStart('v')
            $assetName = "actions-runner-win-x64-$version.zip"
            $asset = @($release.assets | Where-Object { $_.name -eq $assetName })[0]
            if (-not $asset) { throw "Release $($release.tag_name) has no $assetName." }
            if ($release.body -notmatch '<!-- BEGIN SHA win-x64 -->(?<sha>[0-9a-fA-F]{64})<!-- END SHA win-x64 -->') {
                throw "Release $($release.tag_name) notes carry no win-x64 SHA-256; refusing an unverified download."
            }
            $expected = $Matches.sha.ToUpperInvariant()
            $zip = Join-Path $root $assetName
            Write-Host "Downloading $assetName ($([math]::Round($asset.size / 1MB)) MB) from actions/runner $($release.tag_name)."
            Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
            $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
            if ($actual -ne $expected) { Remove-Item -LiteralPath $zip; throw "SHA-256 mismatch for ${assetName}: $actual, expected $expected." }
            Expand-Archive -LiteralPath $zip -DestinationPath $root
            Remove-Item -LiteralPath $zip
        }
        $guard = Join-Path $root 'CIJobGuard.ps1'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CIJobGuard.ps1') -Destination $guard -Force
        $token = & gh api -X POST "repos/$repo/actions/runners/registration-token" --jq '.token'
        if ($LASTEXITCODE -ne 0 -or -not $token) { throw "Could not get a runner registration token for $repo (needs admin access)." }
        & (Join-Path $root 'config.cmd') --unattended --url "https://github.com/$repo" --token $token --name $RunnerName --labels $runnerLabel --work w --replace
        if ($LASTEXITCODE -ne 0) { throw "config.cmd failed with exit code $LASTEXITCODE." }
        Set-DotEnv -Path (Join-Path $root '.env') -Values @{
            LD_UE_ROOT = $engine
            LD_CI_ALLOWED_REPOSITORY = $repo
            ACTIONS_RUNNER_HOOK_JOB_STARTED = $guard
        }
        $settings = Get-LocalSettings
        $settings | Add-Member -NotePropertyName ciRunnerRoot -NotePropertyValue $root -Force
        New-Item -ItemType Directory -Path (Split-Path $localSettingsPath) -Force | Out-Null
        $settings | ConvertTo-Json | Set-Content -LiteralPath $localSettingsPath -Encoding utf8
        Write-Host "Runner '$RunnerName' registered for $repo in $root (label $runnerLabel). It stays off until: .\Scripts\CIRunner.ps1 Start"
    }
    'Start' {
        $root = Resolve-RunnerRoot
        if (@(Get-RunnerProcesses $root | Where-Object Name -eq 'Runner.Listener.exe').Count) { Write-Host 'Runner is already on.'; break }
        # One runner takes one job at a time, so a single listener per machine caps CI at one job at a time.
        $others = @(Get-CimInstance Win32_Process -Filter "Name='Runner.Listener.exe'")
        if ($others.Count) { throw "Another Actions runner is already running on this machine ($($others.ExecutablePath -join ', ')). Stop it first; only one CI job may run here at a time." }
        # The runner writes diagnostic logs for every job and never prunes them.
        $diag = Join-Path $root '_diag'
        if (Test-Path -LiteralPath $diag) {
            Get-ChildItem -LiteralPath $diag -Recurse -File | Where-Object LastWriteTime -lt (Get-Date).AddDays(-14) | Remove-Item -Force
        }
        # Refresh the job guard from this checkout so allowlist changes apply without reinstalling.
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CIJobGuard.ps1') -Destination (Join-Path $root 'CIJobGuard.ps1') -Force
        # BELOWNORMAL is inherited by the listener, the job and every build tool it starts.
        $run = Join-Path $root 'run.cmd'
        Start-Process -FilePath 'cmd.exe' -ArgumentList "/c start `"Lockdown CI runner`" /belownormal /min `"$run`"" -WorkingDirectory $root -WindowStyle Hidden
        $deadline = (Get-Date).AddSeconds(45)
        do {
            Start-Sleep -Seconds 3
            $remote = Get-RemoteRunner (Resolve-Repository)
        } while ((-not $remote -or $remote.status -ne 'online') -and (Get-Date) -lt $deadline)
        if (-not $remote -or $remote.status -ne 'online') { throw "Runner started locally but GitHub does not report '$RunnerName' online yet. Check the 'Lockdown CI runner' window." }
        Write-Host "Runner on: '$RunnerName' is online and will take queued nightly and PR jobs. Turn off with: .\Scripts\CIRunner.ps1 Stop"
    }
    'Stop' {
        $root = Resolve-RunnerRoot
        $processes = @(Get-RunnerProcesses $root)
        if (-not $processes.Count) { Write-Host 'Runner is already off.'; break }
        $busy = @($processes | Where-Object Name -eq 'Runner.Worker.exe').Count -gt 0
        foreach ($process in $processes) { Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue }
        if ($busy) { Write-Host "Runner off. A job was running and has been killed; GitHub marks that run failed." }
        else { Write-Host 'Runner off. Queued jobs wait on GitHub (up to 24 h) for the next Start.' }
    }
    'Status' {
        $root = Resolve-RunnerRoot
        $repo = Resolve-Repository
        $processes = @(Get-RunnerProcesses $root)
        $on = @($processes | Where-Object Name -eq 'Runner.Listener.exe').Count -gt 0
        $busy = @($processes | Where-Object Name -eq 'Runner.Worker.exe').Count -gt 0
        $remote = Get-RemoteRunner $repo
        Write-Host ('Local:  {0}{1}' -f ($(if ($on) { 'on' } else { 'off' })), ($(if ($busy) { ', running a job' } else { '' })))
        Write-Host ('GitHub: {0}' -f ($(if ($remote) { "$($remote.status)$(if ($remote.busy) { ', busy' })" } else { "no runner named $RunnerName" })))
        if ($remote -and -not $on -and $remote.status -eq 'online') { Write-Host '        (GitHub marks a stopped runner offline within a few minutes; no job can start on it meanwhile.)' }
        $queued = & gh run list -R $repo --status queued --json workflowName,displayTitle | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0) { throw "gh run list failed for $repo." }
        Write-Host "Queued runs: $(@($queued).Count)"
        foreach ($run in @($queued)) { Write-Host "  $($run.workflowName): $($run.displayTitle)" }
    }
    'Uninstall' {
        $root = Resolve-RunnerRoot
        $repo = Resolve-Repository
        foreach ($process in Get-RunnerProcesses $root) { Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue }
        $token = & gh api -X POST "repos/$repo/actions/runners/remove-token" --jq '.token'
        if ($LASTEXITCODE -ne 0 -or -not $token) { throw "Could not get a runner removal token for $repo." }
        & (Join-Path $root 'config.cmd') remove --token $token
        if ($LASTEXITCODE -ne 0) { throw "config.cmd remove failed with exit code $LASTEXITCODE." }
        Write-Host "Runner '$RunnerName' unregistered. Its folder $root (checkout and build output) is left in place; delete it yourself when no longer needed."
    }
}
