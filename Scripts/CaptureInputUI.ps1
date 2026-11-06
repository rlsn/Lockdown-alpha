[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(1280,2560)][int]$Width=1280,[ValidateRange(720,1440)][int]$Height=720,[ValidateRange(60,300)][int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
$binary=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$output=New-LDArtifactDirectory ('InputUI-'+$Width+'x'+$Height+'-Frontend')
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$arguments='"{0}" /Game/Lockdown/Generated/Maps/L_Frontend -game -windowed -RenderOffScreen -ForceRes -ResX={1} -ResY={2} -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{3}" -LDDemoFrontendUIProbe -LDDemoFrontendUIOutput="{4}" -abslog="{5}"' -f $project,$Width,$Height,$user,$output,(Join-Path $output 'Visual.log')
@{display=(Get-LDDisplayEnvironment);sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);width=$Width;height=$Height;method='Existing Demo frontend RHI/UMG fixture, isolated UserDir, native screenshots at 100/125 percent text scale; synthetic delegate clicks, not OS input or gameplay acceptance'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Native UI capture timed out: $output"}
    if($process.ExitCode -ne 0){throw "Native UI capture failed: $output"}
    $reportPath=Join-Path $output 'FrontendSettings.json'
    if(-not(Test-Path -LiteralPath $reportPath)){throw "Missing native UI report: $output"}
    $report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
    if(-not $report.passed -or $report.rhi -in @('None','Null') -or @($report.checks|Where-Object{-not $_.passed}).Count){throw "Failed native frontend capture evidence: $reportPath"}
    foreach($name in @('Frontend-100.png','Settings-Display-100.png','Settings-Audio-100.png','Settings-Controls-100.png','Settings-Camera-100.png','Settings-Interface-100.png','Settings-Display-125.png','Settings-Audio-125.png','Settings-Controls-125.png','Settings-Camera-125.png','Settings-Interface-125.png','DiscardDisplayDraft.png','Settings-RebindConflict.png','Settings-ResetConfirm.png')){
        $image=Join-Path $output $name
        if(-not(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).Length -eq 0){throw "Missing rendered capture: $image"}
    }
    Write-Host "Native frontend UI captured; inspect visually: $output"
} finally {if(-not $process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}}
