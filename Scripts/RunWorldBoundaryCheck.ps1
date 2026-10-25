[CmdletBinding()]
param([string]$EngineRoot,[string[]]$Cases=@('Close','Deadline','TieA','TieB','Damage','Tags','Handoff','Late','Gap','Death','Stun','Interrupt','Displacement','Release','Leases','Shrines','Sources','SelfAction','ActivationDeadline'),[ValidateRange(60,600)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B76-Boundaries'
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
foreach($lane in $Cases){
    if($lane -notin @('Close','Deadline','TieA','TieB','Damage','Tags','Handoff','Late','Gap','Death','Stun','Interrupt','Displacement','Release','Leases','Shrines','Sources','SelfAction','ActivationDeadline')){throw "Unknown boundary case: $lane"}
    $laneDirectory=Join-Path $output $lane
    New-Item -ItemType Directory -Path $laneDirectory | Out-Null
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/L_V07Region?Profile=v07_integration -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDWorldProbe -LDWorldBoundaryProbe -LDWorldRoute=Boundary -LDWorldCase={1} -LDProbeReport="{2}" -abslog="{3}"' -f $project,$lane,(Join-Path $laneDirectory 'Report.json'),(Join-Path $laneDirectory 'Native.log')
    $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$lane boundary timed out: $output"};if($process.ExitCode -ne 0){throw "$lane boundary failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content -LiteralPath (Join-Path $laneDirectory 'Report.json') -Raw | ConvertFrom-Json
    if($report.failures -or @($report.checks).Count -lt 2){throw "Incomplete $lane boundary evidence: $output"}
    & python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $laneDirectory 'Ledger.json');if($LASTEXITCODE -ne 0){throw "$lane independent ledger replay failed"}
}
Write-Host "B76 boundary checks passed. Evidence: $output"
