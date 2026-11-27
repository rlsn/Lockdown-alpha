[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateSet('Frontend','Match','All')][string]$Part='All',
    [ValidateRange(120,1800)][int]$TimeoutSeconds=1500,[switch]$OnScreen,
    # Page-name prefixes (e.g. Lobby_,Match_R1): only these are shot and the run ends after the last of them; the pages
    # on the way there still run, without their shots. Names are in the report and in LDUICoverageProbe.cpp.
    [string[]]$Pages,
    # Also 1280x720 and 125% text for every page (four shots a page instead of one).
    [switch]$AllSizes,
    # The recorded-foot trace in the host log (hundreds of MB a run).
    [switch]$TraceFoot,
    # The host signs up for the first rest's PK itself (an entrant's views) instead of watching and betting.
    [switch]$Entrant,
    # The host records its own frames (the game recorder, never the screen) into this folder for review footage.
    [string]$RecordGame)
# Rendered review captures for the UI coverage list (docs/systems/VISION_AND_UI.md 表现层覆盖清单): the frontend pages
# (main menu, how to play, codex, start page, settings tabs, exit confirmation) and a solo Listen Host room (Lobby, opening
# rest, field HUD, held scoreboard, in-match menu, settings, final field, results and an inspected seat) at 1920x1080
# with 100% text; -AllSizes adds 1280x720 and 125% text. The host skips stage countdowns with the DEV AdvanceCountdown op. Presentation evidence only: the images are for a person to look at; the
# report only proves the run reached every page.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output=New-LDArtifactDirectory 'UICoverage-Capture'
$launch=Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$runs=@()
$probe=''
if($AllSizes -and -not $RecordGame){$probe+=' -LDUICoverageAllSizes'}
if($Pages){
    $probe+=' -LDUICoveragePages='+($Pages -join ',')
    $front=@($Pages | Where-Object {$_ -like 'Front*'})
    if($Part -eq 'All'){$Part=if($front.Count -eq $Pages.Count){'Frontend'}elseif($front.Count -eq 0){'Match'}else{'All'}}
}
if($Part -in 'Frontend','All'){$runs+=[pscustomobject]@{Name='Frontend';Url='/Game/Lockdown/Generated/Maps/L_Frontend';Extra='-LDUICoverageFrontend'+$probe}}
# The guest joins with the handshake a real client sends (wire protocol, product version, catalog digest) on a free port.
$socket=[Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
try{$port=$socket.Client.LocalEndPoint.Port}finally{$socket.Dispose()}
$header=Get-Content -LiteralPath (Join-Path (Get-LDProjectRoot) 'Source\Lockdown\Public\Framework\LDDemoEntry.h') -Raw
if($header -notmatch 'inline\s+constexpr\s+uint32\s+WireProtocol\s*=\s*(\d+)\s*;'){throw 'Cannot read WireProtocol from LDDemoEntry.h'}
$wire=[int]$Matches[1]
$version=Get-LDProjectVersion
$digest=(Get-FileHash -LiteralPath (Join-Path (Get-LDProjectRoot) 'Content\Lockdown\Data\Demo\manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$guestUrl='127.0.0.1:{0}?LDName=Guest?LDWire={1}?LDProduct={2}?LDCatalog={3}' -f $port,$wire,$version,$digest
# The host: the DEV growth fixture, its port, the page filter, and optionally the foot trace and the game recorder.
$hostExtra="-LDDevGrowthFixture -port=$port -multihome=127.0.0.1"
if($Entrant){$hostExtra+=' -LDUICoverageEntrant'}
$hostExtra+=$probe
if($TraceFoot){$hostExtra+=' -ExecCmds="ld.Motion.TraceRecordedFoot 1"'}
# A recorded run keeps one window size (the recorder's frame is the window's back buffer).
if($RecordGame){$hostExtra+=(' -LDRecordGame="{0}" -LDRecordGameWidth=1600' -f $RecordGame)}
if($Part -in 'Match','All'){$runs+=[pscustomobject]@{Name='Match';Url='/Game/Lockdown/Generated/Maps/L_Demo?listen?LDName=UICoverage';Extra=$hostExtra}}
$failed=@()
foreach($run in $runs){
    $log=Join-Path $output "$($run.Name).log"
    $user=Join-Path $output "User-$($run.Name)"
    New-Item -ItemType Directory -Path $user | Out-Null
    $render=if($OnScreen){''}else{'-RenderOffScreen '}
    $arguments='-unattended -nop4 -nosound -windowed '+$render+'-ForceRes -ResX=1920 -ResY=1080 -UserDir="{0}" -LDUICoverageProbe {1} -LDUICoverageOutput="{2}" -abslog="{3}"' -f $user,$run.Extra,$launch.ProbeOutput,$log
    $process=$null;$client=$null
    try {
        $process=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $run.Url -Arguments $arguments) -WindowStyle Hidden -PassThru
        # Results-only pages inject the result: the host plays alone with its AI, so no guest joins.
        $solo=$Pages -and @($Pages | Where-Object {$_ -notlike 'Match_Results*'}).Count -eq 0
        if($run.Name -eq 'Match' -and -not $solo){
            # A real guest joins the host's room over localhost for the guest views and the host's not-ready / ready states.
            Start-Sleep -Seconds 25
            $clientUser=Join-Path $output 'User-Client'
            New-Item -ItemType Directory -Path $clientUser | Out-Null
            $clientArgs='-unattended -nop4 -nosound -windowed -multihome=127.0.0.1 '+$render+'-ForceRes -ResX=1920 -ResY=1080 -UserDir="{0}" -LDUICoverageProbe -LDUICoverageClient'+$(if($AllSizes -and -not $RecordGame){' -LDUICoverageAllSizes'}else{''})+' -LDUICoverageOutput="{1}" -abslog="{2}"' -f $clientUser,$launch.ProbeOutput,(Join-Path $output 'Client.log')
            $client=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $guestUrl -Arguments $clientArgs) -WindowStyle Hidden -PassThru
        }
        # A host that was refused its map or hit a fault never reaches a page: stop on the log line instead of waiting.
        $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while(-not $process.WaitForExit(2000)){
            if([DateTime]::UtcNow -gt $deadline){throw "UI coverage capture ($($run.Name)) timed out: $output"}
            $fatal=if(Test-Path -LiteralPath $log){Select-String -LiteralPath $log -Pattern 'LD_DEMO_BROWSE_REJECTED|Fatal error|Assertion failed|LD_DEMO_AUTHORITY_FAULT' | Select-Object -First 1}
            if($fatal){throw "UI coverage capture ($($run.Name)) stopped on: $($fatal.Line.Trim()) Evidence: $output"}
        }
        if($client -and -not $client.HasExited){Stop-Process -Id $client.Id}
    } finally {
        if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
        if($client -and -not $client.HasExited){Stop-Process -Id $client.Id}
    }
    Complete-LDGameEvidence $launch
    $report=Join-Path $output "UICoverage-$($run.Name).json"
    if(-not (Test-Path -LiteralPath $report)){$failed+="$($run.Name): no report";continue}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if(-not $r.passed){$failed+="$($run.Name): $($r.error)"}
    $errors=@(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|LD_DEMO_AUTHORITY_FAULT')
    if($errors.Count){$failed+="$($run.Name): $($errors[0].Line)"}
    Write-Host "$($run.Name): $(@($r.screenshots).Count) shots; missed: $(@($r.missed) -join ', ')"
}
if($failed.Count){throw "UI coverage capture incomplete: $($failed -join ' | '). Evidence: $output"}
Write-Host "UI coverage captured; inspect visually: $output"
