[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateSet('Frontend','Match','All')][string]$Part='All',
    [ValidateRange(120,1800)][int]$TimeoutSeconds=1500,[switch]$OnScreen)
# Rendered review captures for the UI coverage list (docs/systems/VISION_AND_UI.md 表现层覆盖清单): the frontend pages
# (main menu, how to play, codex, start page, settings tabs, exit confirmation) and a solo Listen Host room (Lobby, opening
# rest, field HUD, held scoreboard, in-match menu, settings, final field, results and an inspected seat), each at
# 1920x1080 and 1280x720 with 100% and 125% text. Presentation evidence only: the images are for a person to look at; the
# report only proves the run reached every page.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output=New-LDArtifactDirectory 'UICoverage-Capture'
$launch=Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$runs=@()
if($Part -in 'Frontend','All'){$runs+=[pscustomobject]@{Name='Frontend';Url='/Game/Lockdown/Generated/Maps/L_Frontend';Extra='-LDUICoverageFrontend'}}
if($Part -in 'Match','All'){$runs+=[pscustomobject]@{Name='Match';Url='/Game/Lockdown/Generated/Maps/L_Demo?listen?LDName=UICoverage';Extra='-LDDevGrowthFixture'}}
$failed=@()
foreach($run in $runs){
    $log=Join-Path $output "$($run.Name).log"
    $user=Join-Path $output "User-$($run.Name)"
    New-Item -ItemType Directory -Path $user | Out-Null
    $render=if($OnScreen){''}else{'-RenderOffScreen '}
    $arguments='-unattended -nop4 -nosound -windowed '+$render+'-ForceRes -ResX=1920 -ResY=1080 -UserDir="{0}" -LDUICoverageProbe {1} -LDUICoverageOutput="{2}" -abslog="{3}"' -f $user,$run.Extra,$launch.ProbeOutput,$log
    $process=$null
    try {
        $process=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url $run.Url -Arguments $arguments) -WindowStyle Hidden -PassThru
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "UI coverage capture ($($run.Name)) timed out: $output"}
    } finally {
        if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
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
