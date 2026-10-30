[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,[int]$Port=7936,[int]$TimeoutSeconds=720)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B92Charter-Network'
$owned=@()
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
function Start-ServiceProcess([string]$Role){
    $destination=if($Role -eq 'Server'){'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB92GameMode?Profile=v09_population?Humans=4'}else{"127.0.0.1:$Port"}
    $mode=if($Role -eq 'Server'){'-server'}else{'-game'}
    $surface=if($Capture -and $Role -eq 'A'){'-dx12 -RenderOffScreen -windowed -ForceRes -ResX=1280 -ResY=720 -B92Capture'}else{'-nullrhi'}
    $arguments='"{0}" {1} {2} {3} -port={4} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -ExecCmds="t.MaxFPS 30,r.ScreenPercentage 75,sg.ShadowQuality 1,r.Shadow.Virtual.MaxPhysicalPages 512,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0" -LDB92ServicesProbe -B92CharterOnly -B92Role={5} -B92Output="{6}" -abslog="{7}"' -f $project,$destination,$mode,$surface,$Port,$Role,$output,(Join-Path $output ($Role+'.log'))
    Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
}
try{
    $owned+=Start-ServiceProcess 'Server'
    Start-Sleep -Seconds 3
    $owned+=Start-ServiceProcess 'A'
    Start-Sleep -Seconds 3
    $owned+=Start-ServiceProcess 'B'
    $reconnected=$false
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    while((Get-Date) -lt $deadline -and -not $owned[0].HasExited){
        $report=Join-Path $output 'Services.json'
        if(-not $reconnected -and (Test-Path -LiteralPath $report)){
            try{$state=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json}catch{$state=$null}
            if($state -and $state.phase -eq 12){$owned+=Start-ServiceProcess 'A2';$reconnected=$true}
        }
        Start-Sleep -Milliseconds 500
    }
    if(-not $owned[0].HasExited){throw "B92 Charter timed out: $output"}
    $owned[0].WaitForExit()
    if($owned[0].ExitCode -ne 0){throw "B92 Charter failed: $output"}
    & python -B (Join-Path $PSScriptRoot 'CheckB92Charter.py') $output
    if($LASTEXITCODE -ne 0){throw "B92 Charter evidence rejected: $output"}
    if($Capture -and -not (Test-Path (Join-Path $output 'Charter.png'))){throw "B92 capture missing: $output"}
    Write-Host "PASS B92 earned Charter journey: $output"
}finally{foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}}
