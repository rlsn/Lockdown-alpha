[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateRange(60,1800)][int]$Seconds=600,[ValidateSet(0,100,150)][int]$RoundTripMs=100,[ValidateRange(0,25)][int]$JitterMs=10,[ValidateRange(1024,65000)][int]$Port=7810,[switch]$Headless,[ValidateSet(1,4)][int]$RenderedClients=1)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$prefix='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$prefix='"{0}" ' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject');$lane='Editor-'}
$output=New-LDArtifactDirectory ($lane+"B86-FourClients-$Seconds-$RoundTripMs-$JitterMs")
$minimum=[Math]::Max(0,[int]($RoundTripMs/2)-$JitterMs);$maximum=[int]($RoundTripMs/2)+$JitterMs
@{cpu=@(Get-CimInstance Win32_Processor|Select-Object Name);gpu=@(Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion);memoryBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);durationSeconds=$Seconds;baselineSeconds=30;renderedClients=$(if($Headless){0}else{$RenderedClients});resolution='1280x720';frameCap=60;roundTripMs=$RoundTripMs;jitterMs=$JitterMs;scope='Four independent owning clients; mixed256-cell diagnostic field writes, expiry/reservations, real clocks/native movement. Whole authority/view sample sums; not isolated added environment cost or human readability; rendered-client count recorded separately.'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $output 'Environment.json')
$owned=@()
try {
    foreach($role in @('Server','A','B','C','D')){
        $world=if($role -eq 'Server'){'/Game/Lockdown/Generated/Maps/L_V08Region?Profile=v08_integration'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        if($PackageRoot -and $role -eq 'Server'){$world+='?listen';$mode='-game -LDRemoteAuthority'}
        $surface=if($role -ne 'Server' -and -not $Headless -and ($RenderedClients -eq 4 -or $role -eq 'A')){'-RenderOffScreen -windowed -ForceRes -ResX=1280 -ResY=720'}else{'-nullrhi'}
        $arguments=$prefix+('{0} {1} {2} -port={3} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDB86Probe -LDB86Seconds={4} -LDProbeRole={5} -PktLagMin={6} -PktLagMax={7} -LDProbeReportDir="{8}" -abslog="{9}"' -f $world,$mode,$surface,$Port,$Seconds,$role,$minimum,$maximum,$output,(Join-Path $output "$role.log"))
        $owned+=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds($Seconds+240);$audit=Get-Date
    while(@($owned|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){
        foreach($p in $owned){if($p.HasExited -and $p.ExitCode -ne 0){throw "B86 process failed ($($p.ExitCode)): $output"}}
        if((Get-Date) -ge $audit){foreach($p in $owned|Where-Object{-not $_.HasExited}){$p.Refresh();@{utc=(Get-Date).ToUniversalTime().ToString('o');pid=$p.Id;workingSetBytes=$p.WorkingSet64;privateBytes=$p.PrivateMemorySize64}|ConvertTo-Json -Compress|Add-Content -LiteralPath (Join-Path $output 'Processes.jsonl')};$audit=(Get-Date).AddSeconds(60)}
        Start-Sleep -Milliseconds 500
    }
    foreach($p in $owned){if(-not $p.HasExited){throw "B86 four-client timeout: $output"};if($p.ExitCode -ne 0){throw "B86 process failed ($($p.ExitCode)): $output"}}
    foreach($role in @('Server','A','B','C','D')){$r=Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw|ConvertFrom-Json;if($r.failures -ne 0 -or $r.durationSeconds -lt ($Seconds-1)){throw "Incomplete/failed B86 $role report: $output"};Write-Host "$role passed $(@($r.checks).Count) checks over $($r.durationSeconds)s"}
    Write-Host "B86 four-client fixture passed. Evidence: $output"
} finally {foreach($p in $owned){if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}}
