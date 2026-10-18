[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('All','Assault','Seal')][string]$Scenario='All',[ValidateRange(1024,65000)][int]$Port=7791,[ValidateSet(0,100,150)][int]$RoundTripMs=0,[ValidateRange(0,25)][int]$JitterMs=0,[switch]$Rendered,[ValidateSet('A','B')][string]$RenderRole='A')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory "ObjectiveNetwork-$RoundTripMs-$JitterMs"
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$editor=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$minimum=[Math]::Max(0,[int]($RoundTripMs/2)-$JitterMs)
$maximum=[int]($RoundTripMs/2)+$JitterMs
$scenarios=if($Scenario -eq 'All'){@('Assault','Seal')}else{@($Scenario)}
foreach($testCase in $scenarios) {
    $directory=Join-Path $output $testCase
    New-Item -ItemType Directory -Path $directory | Out-Null
    $ownedProcesses=@()
    try {
        foreach($role in @('Server','A','B')) {
            $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_MigrationSandbox'}else{"127.0.0.1:$Port`?Name=Objective$role"}
            $mode=if($role -eq 'Server'){'-server'}else{'-game'}
            $surface=if($Rendered -and $role -eq $RenderRole){if($role -eq 'A'){'-windowed -ResX=1280 -ResY=720'}else{'-windowed -ResX=1920 -ResY=1080'}}else{'-nullrhi -unattended'}
            $arguments='"{0}" {1} {2} -port={3} {4} -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDObjectiveNetProbe -LDObjectiveScenario={5} -PktLagMin={6} -PktLagMax={7} -LDExpectedRTT={8} -LDProbeRole={9} -LDProbeReportDir="{10}" -abslog="{11}"' -f $project,$world,$mode,$Port,$surface,$testCase,$minimum,$maximum,$RoundTripMs,$role,$directory,(Join-Path $directory "$role.log")
            $style=if($Rendered -and $role -eq $RenderRole){'Normal'}else{'Hidden'}
            $ownedProcesses+=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle $style -PassThru
            if($role -eq 'Server'){Start-Sleep -Seconds 3}
        }
        $deadline=(Get-Date).AddSeconds(175)
        while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
        foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Objective $testCase timed out: $output"};if($process.ExitCode -ne 0){throw "Objective process failed ($($process.ExitCode)): $directory"}}
        foreach($role in @('Server','A','B')) {
            $path=Join-Path $directory "$role.json"
            if(-not(Test-Path -LiteralPath $path)){throw "Missing $role report: $directory"}
            $report=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
            $required=if($role -eq 'Server'){15}else{18}
            if($report.failures -ne 0 -or @($report.checks).Count -lt $required){$report.checks|Write-Host;throw "Failed or incomplete $testCase $role evidence: $path"}
            Write-Host "$testCase $role passed $(@($report.checks).Count) checks"
        }
    } finally {
        foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    }
}
Write-Host "Objective network scenarios passed. Evidence: $output"
