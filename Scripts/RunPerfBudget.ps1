[CmdletBinding()]
param([string]$EngineRoot, [string]$PackageRoot,
    [ValidateRange(1280,3840)][int]$Width=1920, [ValidateRange(720,2160)][int]$Height=1080,
    [ValidateRange(120,900)][int]$TimeoutSeconds=600,
    # Presents to a visible window like a player's session; the default renders off screen without taking focus.
    [switch]$Visible,
    # Plays a real match against the most AI a room takes (Development phase, the Host's game camera) instead of the Lobby field.
    [switch]$Match,
    # Records an Unreal Insights CPU trace (PerfBudget.utrace) of the whole run for attributing the frame time.
    [switch]$Trace,
    # Extra command-line arguments for an A/B comparison, e.g. '-ExecCmds="r.Lumen.HardwareRayTracing 0"'.
    [string]$ExtraArguments)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output = New-LDArtifactDirectory ($(if ($PackageRoot) { 'DemoPerfBudget-Packaged' } else { 'DemoPerfBudget' }) + $(if ($Match) { '-Match' } else { '' }) + "-$Width`x$Height")
$launch = Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$user = Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$window = if ($Visible) { '-windowed' } else { '-windowed -RenderOffScreen' }
$arguments = '-unattended -nop4 -nosound {0} -ForceRes -ResX={1} -ResY={2} -UserDir="{3}" -LDDemoPerfBudget -LDDemoPerfBudgetOutput="{4}" -abslog="{5}"{6}' -f $window,$Width,$Height,$user,$launch.ProbeOutput,(Join-Path $output 'PerfBudget.log'),($(if ($Match) { ' -LDDemoPerfBudgetMatch' } else { '' }) + $(if ($Trace) { ' -trace=cpu,frame,bookmark -statnamedevents -tracefile="{0}"' -f (Join-Path $output 'PerfBudget.utrace') } else { '' }) + $(if ($ExtraArguments) { ' ' + $ExtraArguments } else { '' }))
@{display=(Get-LDDisplayEnvironment);sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);packaged=$launch.Packaged;package=$launch.Package;width=$Width;height=$Height;visible=[bool]$Visible;match=[bool]$Match;extraArguments=$ExtraArguments
    method=$(if ($Match) { 'DEV frame budget fixture in a real Demo match (Listen Host, 3 AI, Development phase): the Host game camera, uncapped, the match alone and with a crowd of every contender seat and camp tier at the low and high presets' } else { 'DEV frame budget fixture in the Demo map Lobby (Listen Host): game camera over open floor, uncapped, empty field and a crowd of every contender seat and camp tier at the low and high presets' })} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
$process = Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url ((Get-LDDemoMaps)[1] + '?listen') -Arguments $arguments) -WindowStyle Hidden -PassThru
try {
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { throw "Frame budget fixture timed out: $output" }
    Complete-LDGameEvidence $launch
    $reportPath = Join-Path $output 'PerfBudget.json'
    if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing frame budget report: $output" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($process.ExitCode -ne 0 -or -not $report.passed -or @($report.checks | Where-Object { -not $_.passed }).Count) { throw "Frame budget measurement failed: $reportPath" }
    $primers = @($report.primers)
    if ($primers.Count -ne 2) { throw "Frame budget measurement did not prime both quality tiers: $reportPath" }
    foreach ($tier in @('low','high')) {
        $primer = @($primers | Where-Object { $_.tier -eq $tier })
        if ($primer.Count -ne 1 -or $primer[0].elapsed_seconds -lt 4 -or $primer[0].frames -lt 1) {
            throw "Frame budget measurement did not prime $tier with the crowd for four seconds: $reportPath"
        }
    }
    $report.windows | Format-Table tier, scene, frames, @{n='fps';e={[math]::Round($_.fps,1)}}, @{n='frame';e={[math]::Round($_.frame_ms,2)}},
        @{n='p95';e={[math]::Round($_.frame_p95_ms,2)}}, @{n='game';e={[math]::Round($_.game_ms,2)}}, @{n='render';e={[math]::Round($_.render_ms,2)}},
        @{n='rhi';e={[math]::Round($_.rhi_ms,2)}}, @{n='gpu';e={[math]::Round($_.gpu_ms,2)}}, budget_ms, within_budget -AutoSize | Out-String | Write-Host
    $mode = if ($launch.Packaged) { 'packaged' } else { 'editor -game (editor overhead included; the packaged result is authoritative)' }
    if (-not $report.budget_passed) { throw "The crowd exceeds the frame budget ($mode): $reportPath" }
    Write-Host "DEV-only frame budget passed on this machine ($mode, $($report.width)x$($report.height), $($report.adapter)). Evidence: $output"
}
finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    Complete-LDGameEvidence $launch
}
