[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(30,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'ObjectiveCheck'
foreach($scenario in @('Main','ExactDeadline','LateClaim','NoClaim','Withdraw')) {
    $reportPath=Join-Path $output "$scenario.json"
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/L_MigrationSandbox -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -fps=60 -LDObjectiveProbe -LDObjectiveCase={1} -LDProbeReport="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$scenario,$reportPath,(Join-Path $output "$scenario.log")
    $process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){$process.Kill();throw "Objective $scenario timed out: $output"}
    if($process.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $reportPath)){throw "Objective $scenario failed or omitted report: $output"}
    $report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
    $required=if($scenario -eq 'Main'){53}elseif($scenario -eq 'Withdraw'){5}else{3}
    if($report.failures -ne 0 -or @($report.checks).Count -lt $required){$report.checks|Write-Host;throw "Objective $scenario evidence failed or incomplete: $reportPath"}
    Write-Host "$scenario passed $(@($report.checks).Count) checks"
}
Write-Host "Objective scenarios passed. Evidence: $output"
