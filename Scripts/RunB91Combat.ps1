[CmdletBinding()]
param([string]$EngineRoot,[int]$Port=7912,[int]$TimeoutSeconds=360)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
& python -B (Join-Path $PSScriptRoot 'SyncB91Forest.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 fitted forest contract is stale'}
$output=New-LDArtifactDirectory 'B91Combat-Network'
$owned=@()
try{
    foreach($role in @('Server','A','B')){
        $destination=if($role -eq 'Server'){'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $arguments='"{0}" {1} {2} -nullrhi -port={3} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 60" -LDB91NetProbe -LDB91CombatProbe -B91Role={4} -B91Output="{5}" -abslog="{6}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$destination,$mode,$Port,$role,$output,(Join-Path $output "$role.log")
        $owned+=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    if(-not $owned[0].WaitForExit($TimeoutSeconds*1000)){throw "Combat timed out: $output"}
    if($owned[0].ExitCode -ne 0){throw "Combat failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB91Combat.py') $output
    if($LASTEXITCODE -ne 0){throw "Combat evidence rejected: $output"}
    Write-Host "B91 combat evidence: $output"
}finally{foreach($p in $owned){if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}}}
