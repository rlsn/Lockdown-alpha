[CmdletBinding()]
param([string]$EngineRoot,[int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B92Economy-Legacy'
foreach($case in @(@{Name='Commerce';Flag='LDCommerceProbe';Event='B62Verified';Minimum=20},@{Name='Institutions';Flag='LDInstitutionProbe';Event='B64Verified';Minimum=36})){
    $folder=Join-Path $output $case.Name
    New-Item -ItemType Directory -Path $folder | Out-Null
    $started=Get-Date
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -{1} -LDProbeReport="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$case.Flag,(Join-Path $folder ($case.Name+'.json')),(Join-Path $folder ($case.Name+'.log'))
    $process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try{
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Legacy $($case.Name) timed out: $output"}
        if($process.ExitCode -ne 0){throw "Legacy $($case.Name) failed: $output"}
    }finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content (Join-Path $folder ($case.Name+'.json')) -Raw | ConvertFrom-Json
    if($report.failures -or $report.checks.Count -lt $case.Minimum -or @($report.checks | Where-Object {-not $_.StartsWith('PASS ')}).Count){throw "Incomplete legacy $($case.Name) report: $output"}
    $ledger=@(Get-ChildItem (Join-Path (Get-LDProjectRoot) ('Saved/Economy/*'+$case.Event+'*.json')) | Where-Object {$_.LastWriteTime -ge $started})
    if($ledger.Count -ne 1){throw "Expected one current $($case.Event) ledger: $output"}
    Copy-Item -LiteralPath $ledger[0].FullName -Destination (Join-Path $folder 'Ledger.json')
    & python -B (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $folder 'Ledger.json')
    if($LASTEXITCODE -ne 0){throw "Legacy $($case.Name) ledger rejected: $output"}
    Write-Host "PASS legacy $($case.Name): $($report.checks.Count) checks"
}
Write-Host "PASS B92 economy legacy regressions: $output"
