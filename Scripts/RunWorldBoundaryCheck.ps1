[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('v07_integration','v08_integration')][string]$Profile='v07_integration',[string[]]$Cases=@('Close','Deadline','TieA','TieB','Damage','Tags','Handoff','Late','Gap','Death','Stun','Interrupt','Displacement','Release','Leases','Shrines','Sources','SelfAction','ActivationDeadline'),[ValidateRange(60,600)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($Profile -eq 'v08_integration' -and -not $PSBoundParameters.ContainsKey('Cases')){$Cases+='NPC'}
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$stage=if($Profile -eq 'v08_integration'){'B85'}else{'B76'}
$map=if($Profile -eq 'v08_integration'){'L_V08Region'}else{'L_V07Region'}
$output=New-LDArtifactDirectory "$stage-Boundaries"
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
foreach($lane in $Cases){
    if($lane -notin @('Close','Deadline','TieA','TieB','Damage','Tags','Handoff','Late','Gap','Death','Stun','Interrupt','Displacement','Release','Leases','Shrines','Sources','SelfAction','ActivationDeadline','NPC')){throw "Unknown boundary case: $lane"}
    $laneDirectory=Join-Path $output $lane
    New-Item -ItemType Directory -Path $laneDirectory | Out-Null
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/{4}?Profile={5} -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDWorldProbe -LDWorldBoundaryProbe -LDWorldRoute=Boundary -LDWorldCase={1} -LDProbeReport="{2}" -abslog="{3}"' -f $project,$lane,(Join-Path $laneDirectory 'Report.json'),(Join-Path $laneDirectory 'Native.log'),$map,$Profile
    $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$lane boundary timed out: $output"};if($process.ExitCode -ne 0){throw "$lane boundary failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content -LiteralPath (Join-Path $laneDirectory 'Report.json') -Raw | ConvertFrom-Json
    if($report.failures -or @($report.checks).Count -lt 2){throw "Incomplete $lane boundary evidence: $output"}
    & python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $laneDirectory 'Ledger.json');if($LASTEXITCODE -ne 0){throw "$lane independent ledger replay failed"}
}
Write-Host "$stage boundary checks passed. Evidence: $output"
