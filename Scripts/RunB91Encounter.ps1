[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[int]$Port=7913,[int]$TimeoutSeconds=360)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB91Forest.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 fitted forest contract is stale'}
$output=New-LDArtifactDirectory 'B91Encounter-Network'
$owned=@()
try{
    foreach($role in @('Server','A','B')){
        $destination=if($role -eq 'Server'){'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $arguments='"{0}" {1} {2} -nullrhi -port={3} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB91NetProbe -LDB91EncounterProbe -B91Role={4} -B91Output="{5}" -abslog="{6}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$destination,$mode,$Port,$role,$output,(Join-Path $output "$role.log")
        if($Capture -and $role -eq 'A'){$arguments=$arguments.Replace('-nullrhi','-RenderOffScreen -windowed -ForceRes -ResX=1280 -ResY=720 -LDB91EncounterCapture').Replace('t.MaxFPS 60','t.MaxFPS 30,sg.ShadowQuality 0,r.Shadow.Virtual.Enable 0,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.PostProcessQuality 0,r.Streaming.PoolSize 128,r.ScreenPercentage 75')}
        $owned+=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    if(-not $owned[0].WaitForExit($TimeoutSeconds*1000)){throw "Encounter timed out: $output"}
    if($owned[0].ExitCode -ne 0){throw "Encounter failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB91Encounter.py') $output
    if($LASTEXITCODE -ne 0){throw "Encounter evidence rejected: $output"}
    Write-Host "B91 encounter evidence: $output"
}finally{foreach($p in $owned){if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}}

$resumeArguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment -server -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB91EncounterResume -LDB91Restore="{1}" -B91Output="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),(Join-Path $output 'EarnedWorld.json'),$output,(Join-Path $output 'Resume.log')
$p=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $resumeArguments -WindowStyle Hidden -PassThru
try{if(-not $p.WaitForExit(180000)){throw "Encounter resume timed out: $output"};if($p.ExitCode -ne 0){throw "Encounter resume failed: $output"};$r=Get-Content (Join-Path $output 'Resume.json') -Raw | ConvertFrom-Json;if(-not $r.complete -or $r.failures){throw "Encounter resume evidence rejected: $output"};Write-Host "PASS earned encounter restart: $output"}finally{if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}
