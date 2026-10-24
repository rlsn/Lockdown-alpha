[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(1024,65000)][int]$Port=7797,[ValidateSet(0,100,150)][int]$RoundTripMs=100,[ValidateRange(0,25)][int]$JitterMs=10,[ValidateRange(60,180)][int]$TimeoutSeconds=120)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory "B71-Tactical-$RoundTripMs-$JitterMs"
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$minimum=[Math]::Max(0,[int]($RoundTripMs/2)-$JitterMs)
$maximum=[int]($RoundTripMs/2)+$JitterMs
$ownedProcesses=@()
try {
    foreach($role in @('Server','A','B')) {
        $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=v07_integration'}else{"127.0.0.1:$Port`?Name=B71$role"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $arguments='"{0}" {1} {2} -port={3} -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDTacticalProbe -LDProbeRole={4} -PktLagMin={5} -PktLagMax={6} -LDProbeReportDir="{7}" -abslog="{8}"' -f $project,$world,$mode,$Port,$role,$minimum,$maximum,$output,(Join-Path $output "$role.log")
        $ownedProcesses+=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "B71 fixture timed out: $output"};if($process.ExitCode -ne 0){throw "B71 fixture failed ($($process.ExitCode)): $output"}}
    foreach($role in @('Server','A','B')) {
        $path=Join-Path $output "$role.json"
        if(-not(Test-Path -LiteralPath $path)){throw "Missing $role report: $output"}
        $report=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
        if($report.failures -ne 0 -or @($report.checks).Count -lt 10){throw "Incomplete/failed $role report: $path"}
        Write-Host "$role passed $(@($report.checks).Count) checks"
    }
    Write-Host "B71 native network fixture passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
