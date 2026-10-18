[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$prefix='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$prefix='"{0}" ' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject');$lane=''}
$output=New-LDArtifactDirectory "$($lane)ResetCheck"
$arguments=$prefix+('/Game/Lockdown/Generated/Maps/L_MigrationSandbox -game -nullrhi -unattended -nosound -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60,gc.CollectGarbageEveryFrame 10" -LDResetProbe -LDProbeReport="{0}" -abslog="{1}"' -f (Join-Path $output 'Reset.json'),(Join-Path $output 'Reset.log'))
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(180000)){throw "Reset probe timed out: $output"}
    $report=Get-Content -LiteralPath (Join-Path $output 'Reset.json') -Raw | ConvertFrom-Json
    if($process.ExitCode -ne 0 -or $report.failures -ne 0 -or $report.restarts -ne 10 -or @($report.checks).Count -lt 80){$report.checks | Write-Host;throw "Reset evidence failed: $output"}
    Write-Host "Ten actual world restarts passed $(@($report.checks).Count) checks. Evidence: $output"
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
