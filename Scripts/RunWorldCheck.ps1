[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('v07_integration','v08_integration')][string]$Profile='v07_integration',[ValidateSet('Keys','Siege','Artifact','Charter','All')][string]$Route='All',[ValidateRange(60,1800)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$stage=if($Profile -eq 'v08_integration'){'B85'}else{'B76'}
$map=if($Profile -eq 'v08_integration'){'L_V08Region'}else{'L_V07Region'}
$output=New-LDArtifactDirectory "$stage-World"
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$routes=if($Route -eq 'All'){@('Keys','Siege','Artifact','Charter')}else{@($Route)}
foreach($lane in $routes){
    $laneDirectory=Join-Path $output $lane
    New-Item -ItemType Directory -Path $laneDirectory | Out-Null
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/{4}?Profile={5} -game -benchmark -fps=20 -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDWorldProbe -LDWorldRoute={1} -LDProbeReport="{2}" -abslog="{3}"' -f $project,$lane,(Join-Path $laneDirectory 'Report.json'),(Join-Path $laneDirectory 'Native.log'),$map,$Profile
    $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$lane native route timed out: $output"};if($process.ExitCode -ne 0){throw "$lane native route failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content -LiteralPath (Join-Path $laneDirectory 'Report.json') -Raw | ConvertFrom-Json
    if($report.failures -or @($report.checks).Count -lt 15){throw "Incomplete $lane route evidence: $output"}
    & python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $laneDirectory 'Ledger.json');if($LASTEXITCODE -ne 0){throw "$lane independent ledger replay failed"}
}
if($Profile -eq 'v08_integration' -and $Route -eq 'All'){
    & python -B (Join-Path $PSScriptRoot 'CheckB85Evidence.py') $output
    if($LASTEXITCODE -ne 0){throw "B85 route/source reconciliation failed: $output"}
}
Write-Host "$stage native route checks passed. Evidence: $output"
