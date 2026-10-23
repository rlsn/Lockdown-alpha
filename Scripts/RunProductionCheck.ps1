[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B63-Production'
$runStarted=Get-Date
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$rulesReport=Join-Path $output 'Rules'
$probeReport=Join-Path $output 'Authority.json'
$lanes=@(
    @{Name='Rules';Args=('"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="Automation RunTests Lockdown.Rules" -TestExit="Automation Test Queue Empty" -ReportExportPath="{1}" -abslog="{2}"' -f $project,$rulesReport,(Join-Path $output 'Rules.log'))},
    @{Name='Authority';Args=('"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDProductionProbe -LDProbeReport="{1}" -abslog="{2}"' -f $project,$probeReport,(Join-Path $output 'Authority.log'))}
)
foreach($lane in $lanes){
    $process=Start-Process -FilePath $binary -ArgumentList $lane.Args -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$($lane.Name) timed out: $output"};if($process.ExitCode -ne 0){throw "$($lane.Name) failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
$rules=Get-Content -LiteralPath (Join-Path $rulesReport 'index.json') -Raw|ConvertFrom-Json
foreach($name in @('Lockdown.Rules.EconomyAccounting','Lockdown.Rules.EconomyCatalog','Lockdown.Rules.CargoCapacity','Lockdown.Rules.EconomyPayload','Lockdown.Rules.ViewPrivacySchema','Lockdown.Rules.ViewPayload')){
    if(-not @($rules.tests|Where-Object{$_.fullTestPath -eq $name -and $_.state -eq 'Success'}).Count){throw "Missing passing $name evidence: $output"}
}
if($rules.failed -or $rules.notRun -or $rules.inProcess){throw "Native rule failures: $output"}
$authority=Get-Content -LiteralPath $probeReport -Raw|ConvertFrom-Json
if($authority.failures -or @($authority.checks).Count -lt 49){throw "Authority checks incomplete: $output"}
$verified=@(Get-ChildItem -Path (Join-Path (Get-LDProjectRoot) 'Saved/Economy/*B63Verified*.json') | Where-Object {$_.LastWriteTime -ge $runStarted})
if($verified.Count -ne 1){throw "Missing unique current-run ledger evidence: $output"}
Copy-Item -LiteralPath $verified[0].FullName -Destination $output
& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output $verified[0].Name)
if($LASTEXITCODE -ne 0){throw "Independent receipt replay failed: $output"}
@{sourceRevision=(git rev-parse HEAD);dirty=[bool](git status --porcelain);rules=$rules.succeeded;authorityChecks=@($authority.checks).Count;scope='B63 rules and two local owners, controlled clock on one authority; no remote-network/human acceptance'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
Write-Host "B63 verification passed: $($rules.succeeded) rules, $(@($authority.checks).Count) authority checks. Evidence: $output"
