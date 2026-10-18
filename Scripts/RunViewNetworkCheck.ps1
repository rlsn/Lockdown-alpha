[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(1024,65000)][int]$Port=7787)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'ViewNetworkCheck'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editor=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$ownedProcesses=@()
try {
    foreach($role in @('Server','A','B')) {
        $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_MigrationSandbox'}else{"127.0.0.1:$Port`?Name=Probe$role"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $arguments='"{0}" {1} {2} -port={3} -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDViewProbe -LDProbeRole={4} -LDProbeReportDir="{5}" -abslog="{6}"' -f $project,$world,$mode,$Port,$role,$output,(Join-Path $output "$role.log")
        $ownedProcesses+=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds(95)
    while(@($ownedProcesses | Where-Object {-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){
        foreach($process in $ownedProcesses){if($process.HasExited -and $process.ExitCode -ne 0){throw "Network process failed ($($process.ExitCode)): $output"}}
        Start-Sleep -Milliseconds 500
    }
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Network scenario timed out: $output"}; if($process.ExitCode -ne 0){throw "Network process failed ($($process.ExitCode)): $output"}}
    foreach($role in @('Server','A','B')) {
        $reportPath=Join-Path $output "$role.json"
        if(-not(Test-Path -LiteralPath $reportPath)){throw "Missing $role report: $output"}
        $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if($report.failures -ne 0 -or @($report.checks).Count -lt 9){throw "Incomplete or failed $role evidence: $reportPath"}
        if($role -ne 'Server' -and @($report.captures).Count -ne 9){throw "Missing client phases: $reportPath"}
        if($role -eq 'Server'){Write-Host "Server passed $(@($report.checks).Count) authority checks"}
        else {Write-Host "$role passed $(@($report.checks).Count) checks; maximum serialized view $($report.maxViewPayloadBytes) bytes"}
    }
    Write-Host "Separate authority plus two restricted clients passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
