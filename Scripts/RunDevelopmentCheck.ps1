[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[switch]$Network,[switch]$Skills,[switch]$Preparation,[switch]$WorldChecks,[switch]$Normal,[switch]$Reset,[switch]$CaptureUI,[ValidateSet(720,1080)][int]$Height=720,[ValidateSet(0,100,150)][int]$RoundTripMs=0,[ValidateRange(0,25)][int]$JitterMs=0,[ValidateRange(1024,65000)][int]$Port=7795)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($PackageRoot) {
    $package=Resolve-LDPackage $PackageRoot; $binary=$package.Binary; $project=''; $lane='Packaged-'
} else {
    $engine=Resolve-LDEngine $EngineRoot; Assert-LDEditorModules
    $binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'; $project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'; $lane=''
}
if(([int][bool]$Network+[int][bool]$Reset+[int][bool]$CaptureUI) -gt 1){throw 'Choose Network, Reset or CaptureUI.'}
$name=if($Network){"DevelopmentNetwork-$RoundTripMs-$JitterMs"}elseif($CaptureUI){"DevelopmentUI-$Height"}elseif($Reset){'DevelopmentReset'}else{'DevelopmentCheck'}
if($Skills){if($Reset){throw 'Skills supports local, Network or CaptureUI verification.'};$name="Skills-$name"}
if($Preparation){if($Skills -or $Reset){throw 'Preparation supports local, Network, Normal or CaptureUI.'};$name="Preparation-$name"}
if($WorldChecks){if($Skills -or $Preparation -or $Reset){throw 'WorldChecks supports local, Network, Normal or CaptureUI.'};$name="WorldChecks-$name"}
if($Normal){if((-not $Preparation -and -not $WorldChecks) -or $Network -or $CaptureUI -or $Reset){throw 'Normal requires Preparation or WorldChecks alone.'};$name=if($WorldChecks){'WorldChecks-Normal'}else{'Preparation-Normal'}}
$output=New-LDArtifactDirectory "$lane$name"
$roles=if($Network){@('Server','A','B')}elseif($CaptureUI){@('Visual')}elseif($Normal){@('Normal')}elseif($WorldChecks){@('WorldChecks')}elseif($Preparation){@('Preparation')}elseif($Skills){@('Skills')}elseif($Reset){@('Reset')}else{@('Local')}
$minimum=[Math]::Max(0,[int]($RoundTripMs/2)-$JitterMs); $maximum=[int]($RoundTripMs/2)+$JitterMs
$ownedProcesses=@()
try {
    foreach($role in $roles) {
        $world=if($role -eq 'A' -or $role -eq 'B'){"127.0.0.1:$Port"}else{'/Game/Lockdown/Generated/Maps/L_MigrationSandbox?Profile=V04'}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        if($PackageRoot -and $role -eq 'Server'){$world+='?listen';$mode='-game -LDRemoteAuthority'}
        $surface=if($CaptureUI){'-windowed -ResX={0} -ResY={1} -unattended' -f ([int]($Height*16/9)),$Height}else{'-nullrhi -unattended'}
        $arguments='"{0}" {1} {2} -port={3} {4} -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDDevelopmentProbe -PktLagMin={5} -PktLagMax={6} -LDProbeRole={7} -LDProbeReportDir="{8}" -abslog="{9}"' -f $project,$world,$mode,$Port,$surface,$minimum,$maximum,$role,$output,(Join-Path $output "$role.log")
        if($Skills){$arguments+=' -LDSkillProbe'}
        if($Preparation){$arguments+=' -LDPreparationProbe'}
        if($WorldChecks){$arguments+=' -LDWorldCheckProbe'}
        if($Normal){$arguments+=if($WorldChecks){' -LDWorldCheckNormal'}else{' -LDPreparationNormal'}}
        if($PackageRoot){$arguments=$arguments.Substring(3)}
        $ownedProcesses+=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds($(if($Normal){400}else{130}))
    while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Development probe timed out: $output"};if($process.ExitCode -ne 0){throw "Development probe failed ($($process.ExitCode)): $output"}}
    foreach($role in $roles) {
        $path=Join-Path $output "$role.json"
        if(-not(Test-Path -LiteralPath $path)){throw "Missing $role report: $output"}
        $report=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
        $required=if($role -eq 'WorldChecks'){56}elseif($role -eq 'Preparation'){61}elseif($role -eq 'Normal'){if($WorldChecks){7}else{14}}elseif($role -eq 'Skills'){62}elseif($role -eq 'Local'){24}elseif($role -eq 'Server'){if($WorldChecks){7}elseif($Preparation){9}elseif($Skills){8}else{6}}elseif($role -eq 'Visual'){1}elseif($role -eq 'Reset'){74}elseif($WorldChecks -or $Preparation -or ($Skills -and $role -eq 'B')){6}else{5}
        if($report.failures -ne 0 -or @($report.checks).Count -lt $required){$report.checks|Write-Host;throw "Failed or incomplete development evidence: $path"}
        Write-Host "$role passed $(@($report.checks).Count) checks"
    }
    Write-Host "Development verification passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
