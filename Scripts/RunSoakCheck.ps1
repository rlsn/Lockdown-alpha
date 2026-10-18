[CmdletBinding()]
param([string]$PackageRoot,[string]$EngineRoot,[ValidateRange(60,600)][int]$Seconds=600,[ValidateSet(0,100,150)][int]$RoundTripMs=150,[ValidateRange(0,25)][int]$JitterMs=25,[ValidateRange(1024,65000)][int]$Port=7795,[switch]$Headless)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$prefix='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$package=[pscustomobject]@{Root=(Get-LDProjectRoot);Binary=(Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')};$prefix='"{0}" ' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject');$lane='Editor-'}
$output=New-LDArtifactDirectory "$($lane)Soak-$Seconds-$RoundTripMs-$JitterMs"
$hardware=@{cpu=@(Get-CimInstance Win32_Processor | Select-Object Name);gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);memoryBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;package=$package.Root;seconds=$Seconds;configuredRTT=$RoundTripMs;jitter=$JitterMs;renderedClients=$(if($Headless){0}else{1});resolution='1920x1080';renderFrameCap=60;authorityFrameCap=120;scalability='High (sg.*=2), DX12 SM6; warmed10s';residents=8}
$hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'Environment.json')
$minimum=[Math]::Max(0,[int]($RoundTripMs/2)-$JitterMs);$maximum=[int]($RoundTripMs/2)+$JitterMs
$owned=@()
try {
    foreach($role in @('Server','A','B')){
        $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_MigrationSandbox?listen'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-game -LDRemoteAuthority'}else{'-game'}
        $render=$role -eq 'A' -and -not $Headless
        $surface=if($render){'-windowed -ResX=1920 -ResY=1080'}else{'-nullrhi -unattended'}
        $commands=if($render){'t.MaxFPS 60,r.VSync 0,r.ScreenPercentage 100,sg.ViewDistanceQuality 2,sg.AntiAliasingQuality 2,sg.ShadowQuality 2,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.PostProcessQuality 2,sg.TextureQuality 2,sg.EffectsQuality 2,sg.FoliageQuality 2,sg.ShadingQuality 2'}else{'t.MaxFPS 120'}
        $arguments='{0} {1} {2} -port={3} -nosound -PktLagMin={4} -PktLagMax={5} -LDExpectedRTT={6} -LDSoakProbe -LDSoakSeconds={7} -LDProbeRole={8} -LDProbeReportDir="{9}" -abslog="{10}" -ExecCmds="{11}"' -f $world,$mode,$surface,$Port,$minimum,$maximum,$RoundTripMs,$Seconds,$role,$output,(Join-Path $output "$role.log"),$commands
        if($role -eq 'Server'){$arguments+=' -ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:NetServerMaxTickRate=120'}
        $arguments=$prefix+$arguments+' -DisablePlugins=AllToolsets,ModelContextProtocol'
        $style=if($render){'Normal'}else{'Hidden'}
        $owned+=Start-Process -FilePath $package.Binary -ArgumentList $arguments -WindowStyle $style -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds($Seconds+240)
    while(@($owned|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
    foreach($process in $owned){if(-not $process.HasExited){throw "Soak timeout: $output"}}
    foreach($role in @('Server','A','B')){$report=Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw|ConvertFrom-Json;$report | ConvertTo-Json -Depth 5 | Write-Host;if($report.failures -ne 0){throw "Soak failed for ${role}: $output"}}
    foreach($process in $owned){if($process.ExitCode -ne 0){throw "Soak process failed: $output"}}
    Write-Host "Packaged soak passed. Evidence: $output"
} finally {foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}}
