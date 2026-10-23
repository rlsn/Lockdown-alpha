[CmdletBinding()]
param([switch]$AuthorityOnly,[switch]$JourneyOnly,[string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($AuthorityOnly -and $JourneyOnly){throw 'Choose one focused lane'}
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B65-Charter'
$runStarted=Get-Date
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$rulesReport=Join-Path $output 'Rules'
$lanes=@(
    @{Name='Rules';Args=('"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="Automation RunTests Lockdown.Rules" -TestExit="Automation Test Queue Empty" -ReportExportPath="{1}" -abslog="{2}"' -f $project,$rulesReport,(Join-Path $output 'Rules.log'))},
    @{Name='Authority';Args=('"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDCharterProbe -LDProbeReport="{1}" -abslog="{2}"' -f $project,(Join-Path $output 'Authority.json'),(Join-Path $output 'Authority.log'))},
    @{Name='Journey';Args=('"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDCharterProbe -LDCharterJourney -LDProbeReport="{1}" -abslog="{2}"' -f $project,(Join-Path $output 'Journey.json'),(Join-Path $output 'Journey.log'))}
)
if($AuthorityOnly){$lanes=@($lanes|Where-Object{$_.Name -eq 'Authority'})}
if($JourneyOnly){$lanes=@($lanes|Where-Object{$_.Name -eq 'Journey'})}
foreach($lane in $lanes){
    $process=Start-Process -FilePath $binary -ArgumentList $lane.Args -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$($lane.Name) timed out: $output"};if($process.ExitCode -ne 0){throw "$($lane.Name) failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
foreach($lane in $lanes){
    if($lane.Name -eq 'Rules'){
        $rules=Get-Content -LiteralPath (Join-Path $rulesReport 'index.json') -Raw|ConvertFrom-Json
        if($rules.failed -or $rules.notRun -or $rules.inProcess -or $rules.succeeded -lt 23){throw "Native rule failures: $output"}
    }else{
        $report=Get-Content -LiteralPath (Join-Path $output "$($lane.Name).json") -Raw|ConvertFrom-Json
        if($report.failures -or @($report.checks).Count -lt 35){throw "Incomplete $($lane.Name) evidence: $output"}
    }
}
$verified=@(Get-ChildItem -Path (Join-Path (Get-LDProjectRoot) 'Saved/Economy/*B65*.json') | Where-Object {$_.LastWriteTime -ge $runStarted})
if($verified.Count -lt 1){throw "Missing current-run ledger evidence: $output"}
foreach($file in $verified){Copy-Item -LiteralPath $file.FullName -Destination $output;& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output $file.Name);if($LASTEXITCODE -ne 0){throw "Independent receipt replay failed: $output"}}
@{sourceRevision=(git rev-parse HEAD);dirty=[bool](git status --porcelain);lanes=@($lanes|ForEach-Object{$_.Name});scope='B65 focused native rules and local authority; controlled clock/placement; earned journey disables diagnostic resources; no human/remote/cooked acceptance'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
Write-Host "B65 verification passed. Evidence: $output"
