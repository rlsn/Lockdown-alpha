[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[ValidateRange(60,1800)][int]$Seconds=900,[int]$Port=7937,[int]$TimeoutSeconds=1200)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory "B92Routines-$Seconds"
$owned=@()
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
try{
    foreach($role in @('Server','A')){
        $destination=if($role -eq 'Server'){'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB92GameMode?Profile=v09_population?Humans=4'}else{"127.0.0.1:$Port"}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        $surface=if($Capture -and $role -eq 'A'){'-dx12 -RenderOffScreen -windowed -ForceRes -ResX=1280 -ResY=720 -B92Capture'}else{'-nullrhi'}
        $arguments='"{0}" {1} {2} {3} -port={4} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 30,r.ScreenPercentage 75,sg.ShadowQuality 1,sg.Shadow.Virtual.MaxPhysicalPages 512,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0" -LDB92RoutinesProbe -B92RoutineSeconds={5} -B92Role={6} -B92Output="{7}" -abslog="{8}"' -f $project,$destination,$mode,$surface,$Port,$Seconds,$role,$output,(Join-Path $output ($role+'.log'))
        $owned+=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    if(-not $owned[0].WaitForExit($TimeoutSeconds*1000)){throw "B92 routines timed out: $output"}
    if($owned[0].ExitCode -ne 0){throw "B92 routines failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB92Routines.py') $output
    if($LASTEXITCODE -ne 0){throw "B92 routine evidence rejected: $output"}
    if($Capture -and -not (Test-Path -LiteralPath (Join-Path $output 'Routines.png'))){throw "Missing routine capture: $output"}
    Write-Host "PASS B92 native routines: $output"
}finally{foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}}
