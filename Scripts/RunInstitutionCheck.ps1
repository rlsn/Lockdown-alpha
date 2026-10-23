[CmdletBinding()]
param([switch]$AuthorityOnly,[switch]$GeographyOnly,[string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($AuthorityOnly -and $GeographyOnly){throw 'Choose one focused lane'}
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B64-Institutions'
$runStarted=Get-Date
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$rules=@{succeeded=0}
$rulesReport=Join-Path $output 'Rules'
$probeReport=Join-Path $output 'Authority.json'
$lanes=@(
    @{Name='Rules';Args=('"{0}" -unattended -nop4 -nosound -nullrhi -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="Automation RunTests Lockdown.Rules" -TestExit="Automation Test Queue Empty" -ReportExportPath="{1}" -abslog="{2}"' -f $project,$rulesReport,(Join-Path $output 'Rules.log'))},
    @{Name='Authority';Args=('"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDInstitutionProbe -LDProbeReport="{1}" -abslog="{2}"' -f $project,$probeReport,(Join-Path $output 'Authority.log'))}
)
if($AuthorityOnly){$lanes=@($lanes|Where-Object{$_.Name -eq 'Authority'})}
else{
    foreach($map in @('L_DevelopmentRegion','L_RegionAlternate')){
        $report=Join-Path $output "$map-Geography.json"
        $lanes+=@{Name="$map-Geography";Args=('"{0}" /Game/Lockdown/Generated/Maps/{1}?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDInstitutionProbe -LDGeography -LDProbeReport="{2}" -abslog="{3}"' -f $project,$map,$report,(Join-Path $output "$map-Geography.log"))}
    }
}
if($GeographyOnly){$lanes=@($lanes|Where-Object{$_.Name -like '*-Geography'})}
foreach($lane in $lanes){
    $process=Start-Process -FilePath $binary -ArgumentList $lane.Args -WindowStyle Hidden -PassThru
    try {if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "$($lane.Name) timed out: $output"};if($process.ExitCode -ne 0){throw "$($lane.Name) failed ($($process.ExitCode)): $output"}}
    finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
if($GeographyOnly){
    foreach($map in @('L_DevelopmentRegion','L_RegionAlternate')){
        $geo=Get-Content -LiteralPath (Join-Path $output "$map-Geography.json") -Raw|ConvertFrom-Json
        if($geo.failures -or @($geo.travel).Count -ne 4){throw "Incomplete $map native geography evidence: $output"}
    }
    @{sourceRevision=(git rev-parse HEAD);dirty=[bool](git status --porcelain);geographyOnly=$true;routes=8;scope='Final placements and native travel on both saved maps at 4x world time; no human/remote claim'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
    Write-Host "B64 geography passed: eight actual native routes. Evidence: $output"
    return
}
if(-not $AuthorityOnly){
$rules=Get-Content -LiteralPath (Join-Path $rulesReport 'index.json') -Raw|ConvertFrom-Json
foreach($name in @('Lockdown.Rules.EconomyAccounting','Lockdown.Rules.EconomyCatalog','Lockdown.Rules.CargoCapacity','Lockdown.Rules.EconomyPayload','Lockdown.Rules.ViewPrivacySchema','Lockdown.Rules.ViewPayload')){
    if(-not @($rules.tests|Where-Object{$_.fullTestPath -eq $name -and $_.state -eq 'Success'}).Count){throw "Missing passing $name evidence: $output"}
}
if($rules.failed -or $rules.notRun -or $rules.inProcess){throw "Native rule failures: $output"}
foreach($map in @('L_DevelopmentRegion','L_RegionAlternate')){
    $geo=Get-Content -LiteralPath (Join-Path $output "$map-Geography.json") -Raw|ConvertFrom-Json
    if($geo.failures -or @($geo.travel).Count -ne 4){throw "Incomplete $map native geography evidence: $output"}
}
}
$authority=Get-Content -LiteralPath $probeReport -Raw|ConvertFrom-Json
if($authority.failures -or @($authority.checks).Count -lt 36){throw "Authority checks incomplete: $output"}
$verified=@(Get-ChildItem -Path (Join-Path (Get-LDProjectRoot) 'Saved/Economy/*B64Verified*.json') | Where-Object {$_.LastWriteTime -ge $runStarted})
if($verified.Count -ne 1){throw "Missing unique current-run ledger evidence: $output"}
Copy-Item -LiteralPath $verified[0].FullName -Destination $output
& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output $verified[0].Name)
if($LASTEXITCODE -ne 0){throw "Independent receipt replay failed: $output"}
@{sourceRevision=(git rev-parse HEAD);dirty=[bool](git status --porcelain);rules=$rules.succeeded;authorityChecks=@($authority.checks).Count;authorityOnly=[bool]$AuthorityOnly;scope='B64 controlled authority, native travel on both saved maps at 4x world time when full lane requested; no remote/human acceptance'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
Write-Host "B64 verification passed: $($rules.succeeded) rules, $(@($authority.checks).Count) authority checks. Evidence: $output"
