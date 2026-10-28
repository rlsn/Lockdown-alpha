[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(30,900)][int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B91Checkpoint'
foreach($case in @('Write','Read','Reject','Fresh')){
    $arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB91CheckpointProbe -B91CheckpointCase={1} -B91Output="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$case,$output,(Join-Path $output "$case.log")
    if($case -eq 'Read'){$arguments+=' -LDB91Restore="{0}"' -f (Join-Path $output 'World.json')}
    $process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try{
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Checkpoint $case timed out: $output"}
        if($process.ExitCode -ne 0){throw "Checkpoint $case failed: $output"}
        $report=Get-Content -LiteralPath (Join-Path $output "$case.json") -Raw | ConvertFrom-Json
        if(-not $report.completed -or $report.failures -ne 0 -or @($report.checks | Where-Object {-not $_.passed}).Count){throw "Checkpoint $case evidence rejected: $output"}
        Write-Host "Checkpoint $case passed: $($report.checks.Count) checks"
    }finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
& python -B (Join-Path $PSScriptRoot 'CheckB91Checkpoint.py') $output --placement
if($LASTEXITCODE -ne 0){throw "Checkpoint evidence rejected: $output"}
Write-Host "Checkpoint evidence: $output"
