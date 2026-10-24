[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B74-Acquisition'
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$lanes=@(@{Name='Journey';Args=('"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=v07_integration -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDAcquisitionProbe -LDProbeReport="{1}" -abslog="{2}"' -f $project,(Join-Path $output 'Journey.json'),(Join-Path $output 'Journey.log'))})
foreach($lane in $lanes){
    $process=Start-Process -FilePath $binary -ArgumentList $lane.Args -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$($lane.Name) timed out: $output"};if($process.ExitCode -ne 0){throw "$($lane.Name) failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
$report=Get-Content -LiteralPath (Join-Path $output 'Journey.json') -Raw|ConvertFrom-Json
if($report.failures -or @($report.checks).Count -lt 35){throw "Incomplete earned journey evidence: $output"}
& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output 'Ledger.json');if($LASTEXITCODE -ne 0){throw 'Independent B74 replay failed'}
@{sourceRevision=(git rev-parse HEAD);dirty=[bool](git status --porcelain);lanes=@($lanes|ForEach-Object{$_.Name});scope='B74 local authority earned acquisition; no resource grants; controlled clock/placement; no human/remote/cooked acceptance'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
Write-Host "B74 verification passed. Evidence: $output"
