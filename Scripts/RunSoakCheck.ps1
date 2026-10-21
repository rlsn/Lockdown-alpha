[CmdletBinding()]
param([string]$PackageRoot,[string]$EngineRoot,[ValidateRange(60,5400)][int]$Seconds=600,[ValidateSet(0,100,150)][int]$RoundTripMs=150,[ValidateRange(0,25)][int]$JitterMs=25,[ValidateRange(1024,65000)][int]$Port=7795,[switch]$Headless,[switch]$V04,[switch]$LifeCycles,[ValidateSet('Default','High')][string]$Quality='Default',[ValidateRange(-1,100)][int]$OneWayBaseMs=-1)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($Seconds -gt 600 -and -not $V04){throw 'Durations above600 require the explicitly labelled V04 technical harness.'}
if($LifeCycles -and -not $V04){throw 'LifeCycles is an explicit V04 technical load fixture.'}
if($LifeCycles -and $Seconds -le 120){throw 'LifeCycles requires more than120 measured seconds.'}
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$prefix='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$package=[pscustomobject]@{Root=(Get-LDProjectRoot);Binary=(Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')};$prefix='"{0}" ' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject');$lane='Editor-'}
$output=New-LDArtifactDirectory "$($lane)$(if($V04){'V04-'})Soak-$Seconds-$RoundTripMs-$JitterMs-$Quality"
if($OneWayBaseMs -lt 0){$OneWayBaseMs=if($V04){[Math]::Max(0,[int](($RoundTripMs-30)/2))}else{[int]($RoundTripMs/2)}}
$hardware=@{cpu=@(Get-CimInstance Win32_Processor | Select-Object Name);gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);memoryBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;package=$package.Root;seconds=$Seconds;targetMeasuredRTT=$RoundTripMs;oneWayBaseMs=$OneWayBaseMs;jitter=$JitterMs;renderedClients=$(if($Headless){0}else{1});resolution='1920x1080';renderFrameCap=60;authorityFrameCap=120;scalability=$Quality;warmupSeconds=10;residents=8;profile=$(if($V04){'V04 technical load fixture, no resource or build grants; deadline extended only above1400 seconds; real clocks; not ordinary pacing'}else{'Legacy technical load'});headlessListenAuthority=$true}
$hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'Environment.json')
@{enabled=[bool]$LifeCycles;periodSeconds=120;overrides='Initial/returned/fixture body placements; fixture target HP=1; attacker action/cadence cleared and immediate lethal impact; no resource/build grants or clock acceleration'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'LifeFixture.json')
$minimum=[Math]::Max(0,$OneWayBaseMs-$JitterMs);$maximum=$OneWayBaseMs+$JitterMs
$owned=@()
try {
    foreach($role in @('Server','A','B')){
        $world=if($role -eq 'Server'){if($V04){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?listen?Profile=V04'}else{'/Game/Lockdown/Generated/Maps/L_MigrationSandbox?listen'}}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-game -LDRemoteAuthority'}else{'-game'}
        $render=$role -eq 'A' -and -not $Headless
        $surface=if($render){'-RenderOffScreen -ForceRes -windowed -ResX=1920 -ResY=1080 -unattended'}else{'-nullrhi -unattended'}
        $commands=if($render -and $Quality -eq 'High'){'t.MaxFPS 60,r.VSync 0,r.ScreenPercentage 100,sg.ViewDistanceQuality 2,sg.AntiAliasingQuality 2,sg.ShadowQuality 2,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.PostProcessQuality 2,sg.TextureQuality 2,sg.EffectsQuality 2,sg.FoliageQuality 2,sg.ShadingQuality 2'}elseif($render){'t.MaxFPS 60'}else{'t.MaxFPS 120'}
        $arguments='{0} {1} {2} -port={3} -nosound -PktLagMin={4} -PktLagMax={5} -LDExpectedRTT={6} -LDSoakProbe -LDSoakSeconds={7} -LDProbeRole={8} -LDProbeReportDir="{9}" -abslog="{10}" -ExecCmds="{11}"' -f $world,$mode,$surface,$Port,$minimum,$maximum,$RoundTripMs,$Seconds,$role,$output,(Join-Path $output "$role.log"),$commands
        if($role -eq 'Server'){$arguments+=' -ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:NetServerMaxTickRate=120'}
        if($V04){$arguments+=' -LDReleaseSoak'}
        if($LifeCycles){$arguments+=' -LDLoadLifeCycles'}
        $arguments=$prefix+$arguments+' -DisablePlugins=AllToolsets,ModelContextProtocol'
        $owned+=Start-Process -FilePath $package.Binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    @(@{role='Server';pid=$owned[0].Id},@{role='A';pid=$owned[1].Id},@{role='B';pid=$owned[2].Id})|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'ProcessTopology.json')
    $deadline=(Get-Date).AddSeconds($Seconds+240)
    $nextMonitor=Get-Date
    while(@($owned|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){
        if((Get-Date) -ge $nextMonitor){
            $gpuSamples=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -ErrorAction SilentlyContinue)
            foreach($process in $owned|Where-Object{-not $_.HasExited}){
                $process.Refresh();$gpuOwned=@($gpuSamples|Where-Object {$_.Name -like "pid_$($process.Id)_*"})
                $dedicated=$null;$shared=$null
                if($gpuOwned.Count){$dedicated=0;$shared=0;foreach($sample in $gpuOwned){$dedicated+=[double]$sample.DedicatedUsage;$shared+=[double]$sample.SharedUsage}}
                @{utc=(Get-Date).ToUniversalTime().ToString('o');pid=$process.Id;workingSetBytes=$process.WorkingSet64;privateBytes=$process.PrivateMemorySize64;gpuDedicatedBytes=$dedicated;gpuSharedBytes=$shared}|ConvertTo-Json -Compress|Add-Content -LiteralPath (Join-Path $output 'ProcessSamples.jsonl')
            }
            $nextMonitor=(Get-Date).AddSeconds(60)
        }
        Start-Sleep -Milliseconds 500
    }
    foreach($process in $owned){if(-not $process.HasExited){throw "Soak timeout: $output"}}
    foreach($role in @('Server','A','B')){$report=Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw|ConvertFrom-Json;Write-Host "$role duration=$($report.durationSeconds) failures=$($report.failures)";if($report.failures -ne 0 -or $report.durationSeconds -lt ($Seconds-1) -or @($report.checks).Count -lt 3){throw "Soak failed or incomplete for ${role}: $output"}}
    foreach($process in $owned){if($process.ExitCode -ne 0){throw "Soak process failed: $output"}}
    Write-Host "Packaged soak passed. Evidence: $output"
} finally {foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}}
