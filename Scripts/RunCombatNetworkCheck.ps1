[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateRange(1024,65000)][int]$Port=7789,[ValidateSet(0,100,150)][int]$RoundTripMs=0,[ValidateRange(0,25)][int]$JitterMs=0,[switch]$Rendered,[ValidateSet('A','B')][string]$RenderRole='B')
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$editor=$package.Binary;$project='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$editor=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject';$lane=''}
$output=New-LDArtifactDirectory "$($lane)CombatNetwork-$RoundTripMs-$JitterMs"
$oneWay=[int]($RoundTripMs/2)
$minimum=[Math]::Max(0,$oneWay-$JitterMs)
$maximum=$oneWay+$JitterMs
$ownedProcesses=@()
try {
    foreach($role in @('Server','A','B')) {
        $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_MigrationSandbox'}else{"127.0.0.1:$Port`?Name=Combat$role"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        if($PackageRoot -and $role -eq 'Server'){$world+='?listen';$mode='-game -LDRemoteAuthority'}
        $surface=if($Rendered -and $role -eq $RenderRole){if($role -eq 'A'){'-windowed -ResX=1280 -ResY=720'}else{'-windowed -ResX=1920 -ResY=1080'}}else{'-nullrhi -unattended'}
        $arguments='"{0}" {1} {2} -port={3} {10} -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDCombatNetProbe -PktLagMin={4} -PktLagMax={5} -LDExpectedRTT={6} -LDProbeRole={7} -LDProbeReportDir="{8}" -abslog="{9}"' -f $project,$world,$mode,$Port,$minimum,$maximum,$RoundTripMs,$role,$output,(Join-Path $output "$role.log"),$surface
        if($PackageRoot){$arguments=$arguments.Substring(3)}
        $windowStyle=if($Rendered -and $role -eq $RenderRole){'Normal'}else{'Hidden'}
        $ownedProcesses+=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle $windowStyle -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds(110)
    while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Combat network timed out: $output"};if($process.ExitCode -ne 0){throw "Combat network process failed ($($process.ExitCode)): $output"}}
    foreach($role in @('Server','A','B')){
        $reportPath=Join-Path $output "$role.json"
        if(-not(Test-Path -LiteralPath $reportPath)){throw "Missing $role report: $output"}
        $report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
        $required=if($role -eq 'Server'){9}elseif($role -eq 'A'){16}else{19}
        if($report.failures -ne 0 -or @($report.checks).Count -lt $required){throw "Incomplete or failed $role evidence: $reportPath"}
        Write-Host "$role passed $(@($report.checks).Count) checks"
    }
    Write-Host "Combat network passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
