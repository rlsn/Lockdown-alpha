[CmdletBinding()]
param([string]$EngineRoot, [ValidateRange(30,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'DemoEntry-Native'
$cases = @(
    @{ Name = 'RetiredWorldURL'; URL = '/Game/Lockdown/Generated/V09/L_V09Full?Name=EntryProbe' },
    @{ Name = 'ExplicitModeOverride'; URL = '/Game/Lockdown/Generated/Maps/L_Frontend?Name=EntryProbe?game=/Script/Engine.GameModeBase' }
)
foreach ($case in $cases) {
    $reportPath = Join-Path $output "$($case.Name).json"
    $logPath = Join-Path $output "$($case.Name).log"
    $arguments = '"{0}" "{1}" -game -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -LDDemoEntryProbe -LDDemoEntryProbeOutput="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$case.URL,$reportPath,$logPath
    $process = Start-Process -FilePath (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id
        throw "Native entry rejection timed out: $($case.Name). Evidence: $output"
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $reportPath)) {
        throw "Native entry rejection failed: $($case.Name). Evidence: $output"
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.passed -or $report.rejected_url -ne $case.URL -or -not $report.failure -or
        $report.arrived_world -ne '/Game/Lockdown/Generated/Maps/L_Frontend' -or
        $report.live_pawns -ne 0 -or $report.legacy_game_modes -ne 0 -or $report.gameplay_services -ne 0 -or
        (-not $report.observed_before_initialization -and -not $report.rejected_before_world_creation) -or $report.rejected_attempt_pawns -ne 0 -or
        $report.rejected_attempt_game_modes -ne 0 -or $report.rejected_attempt_services -ne 0 -or
        $report.registry_creations -ne 0 -or $report.registry_activations -ne 0) {
        throw "Native entry evidence is incomplete or activated legacy gameplay: $reportPath"
    }
}
Write-Host "Native rejected-URL scenarios returned to the frontend without gameplay activation. Evidence: $output"
