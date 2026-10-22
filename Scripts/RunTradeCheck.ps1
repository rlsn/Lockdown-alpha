[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[switch]$Region=$true,[switch]$Terminal,[switch]$Disconnect,[switch]$CaptureUI,[switch]$LargeText,[ValidateSet(1280,1920)][int]$Width=1280,[ValidateSet(720,1080)][int]$Height=720,[ValidateSet(0,100,150)][int]$RoundTripMs=0,[ValidateRange(0,25)][int]$JitterMs=0,[ValidateRange(-1,100)][int]$OneWayBaseMs=-1,[ValidateRange(1024,65000)][int]$Port=7805)
. (Join-Path $PSScriptRoot 'Common.ps1')
if(-not $Region){throw 'Trade peer-exchange verification requires the regional Guild map; use the default Region profile.'}
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$project='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject';$lane=''}
$output=New-LDArtifactDirectory ($lane+'Trade-'+$RoundTripMs+'-'+$JitterMs+$(if($Disconnect){'-Disconnect'}elseif($Terminal){'-Terminal'}else{'-Death'})+$(if($CaptureUI){'-UI'+$Width+'x'+$Height+$(if($LargeText){'-125pct'}else{''})}else{''}))
if($OneWayBaseMs -lt 0){$OneWayBaseMs=[int]($RoundTripMs/2)}
$minimum=[Math]::Max(0,$OneWayBaseMs-$JitterMs);$maximum=$OneWayBaseMs+$JitterMs
@{sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);profile='V05 B56 native peer exchange with labelled state/race fixtures';region=[bool]$Region;ending=$(if($Disconnect){'Disconnect'}elseif($Terminal){'Terminal'}else{'Death'});captureUI=[bool]$CaptureUI;width=$Width;height=$Height;textScale=$(if($LargeText){1.25}else{1});targetRTT=$RoundTripMs;oneWayBaseMs=$OneWayBaseMs;jitterMs=$JitterMs;frameCap=60;topology='Separate authority and two independent owning clients; headless listen authority for packaged lane'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
if($Terminal -and $Disconnect){throw 'Choose Terminal or Disconnect ending.'}
$ownedProcesses=@()
try {
    foreach($role in @('Server','A','B')) {
        $world=if($role -ne 'Server'){"127.0.0.1:$Port"}elseif($Region){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05'}else{'/Game/Lockdown/Generated/Maps/L_MigrationSandbox?Profile=V05'}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        if($PackageRoot -and $role -eq 'Server'){$world+='?listen';$mode='-game -LDRemoteAuthority'}
        $surface=if($CaptureUI -and $role -eq 'A'){"-windowed -RenderOffScreen -ForceRes -ResX=$Width -ResY=$Height"}else{'-nullrhi'}
        $extra=($(if($Terminal){' -LDTradeTerminal'}else{''})+$(if($Disconnect){' -LDTradeDisconnect'}else{''})+$(if($CaptureUI){' -LDTradeCapture'}else{''})+$(if($LargeText){' -LDHUDLargeText'}else{''}))
        $arguments='"{0}" {1} {2} -port={3} {9} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDTradeProbe -LDWorldCheckProbe {10} -PktLagMin={4} -PktLagMax={5} -LDProbeRole={6} -LDProbeReportDir="{7}" -abslog="{8}"' -f $project,$world,$mode,$Port,$minimum,$maximum,$role,$output,(Join-Path $output "$role.log"),$surface,$extra
        if($PackageRoot){$arguments=$arguments.Substring(3)}
        $ownedProcesses+=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds(270)
    while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){foreach($tradeProcess in $ownedProcesses){if($tradeProcess.HasExited -and $tradeProcess.ExitCode -ne 0){throw "Trade process failed ($($tradeProcess.ExitCode)): $output"}};Start-Sleep -Milliseconds 500}
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Trade probe timed out: $output"};if($process.ExitCode -ne 0){throw "Trade probe failed ($($process.ExitCode)): $output"}}
    foreach($role in @('Server','A','B')) {
        $report=Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw|ConvertFrom-Json
        $required=if($role -eq 'Server'){if($Disconnect){41}elseif($Terminal){42}else{43}}elseif($role -eq 'A'){12}else{if($Disconnect){11}else{12}}
        if($report.failures -ne 0 -or @($report.checks).Count -lt $required){throw "Failed or incomplete $role Trade evidence: $output"}
        Write-Host "$role passed $(@($report.checks).Count) checks"
    }
    if($CaptureUI){foreach($tradeImage in @('TradeReview','TradeCompleted','TradeSixItems')){if(-not(Test-Path (Join-Path $output ($tradeImage+'.png')))){throw "Missing native trade image $tradeImage"}}}
    Write-Host "Trade verification passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
