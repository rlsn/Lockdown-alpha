[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('Keys','Siege','Artifact','Charter','All')][string]$Route='All',[ValidateRange(60,1800)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B76-World'
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$routes=if($Route -eq 'All'){@('Keys','Siege','Artifact','Charter')}else{@($Route)}
foreach($lane in $routes){
    $laneDirectory=Join-Path $output $lane
    New-Item -ItemType Directory -Path $laneDirectory | Out-Null
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/L_V07Region?Profile=v07_integration -game -benchmark -fps=20 -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDWorldProbe -LDWorldRoute={1} -LDProbeReport="{2}" -abslog="{3}"' -f $project,$lane,(Join-Path $laneDirectory 'Report.json'),(Join-Path $laneDirectory 'Native.log')
    $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$lane native route timed out: $output"};if($process.ExitCode -ne 0){throw "$lane native route failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content -LiteralPath (Join-Path $laneDirectory 'Report.json') -Raw | ConvertFrom-Json
    if($report.failures -or @($report.checks).Count -lt 15){throw "Incomplete $lane route evidence: $output"}
    & python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $laneDirectory 'Ledger.json');if($LASTEXITCODE -ne 0){throw "$lane independent ledger replay failed"}
}
Write-Host "B76 native route checks passed. Evidence: $output"
