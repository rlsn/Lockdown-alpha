[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(1280,2560)][int]$Width=1600,[ValidateRange(720,1440)][int]$Height=900,[ValidateRange(60,900)][int]$TimeoutSeconds=600)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
$binary=Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$output=New-LDArtifactDirectory ('DemoArt-Capture-'+$Width+'x'+$Height)
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$world=(Get-LDDemoMaps)[1]+'?listen'
$arguments='"{0}" {1} -game -windowed -RenderOffScreen -ForceRes -ResX={2} -ResY={3} -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{4}" -LDDemoArtCapture -LDDemoArtCaptureOutput="{5}" -abslog="{6}"' -f $project,$world,$Width,$Height,$user,$output,(Join-Path $output 'Art.log')
@{display=(Get-LDDisplayEnvironment);sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);width=$Width;height=$Height;method='DEV art fixture in the Demo map Lobby (Listen Host): catalog bodies and clips posed, decorated field rendered from the game camera angle, decoration collision/sight-trace/navmesh checks; not gameplay or combat wiring acceptance'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Art capture timed out: $output"}
    $reportPath=Join-Path $output 'DemoArt.json'
    if(-not(Test-Path -LiteralPath $reportPath)){throw "Missing art capture report: $output"}
    $report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
    if($process.ExitCode -ne 0 -or -not $report.passed -or @($report.checks|Where-Object{-not $_.passed}).Count){throw "Failed art capture evidence: $reportPath"}
    foreach($name in $report.screenshots){
        $image=Join-Path $output $name
        if(-not(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).Length -eq 0){throw "Missing rendered capture: $image"}
    }
    & python -B (Join-Path $PSScriptRoot 'DrawDemoArtLayout.py') --report $reportPath --out (Join-Path $output 'Layout.png')
    if($LASTEXITCODE -ne 0){throw "Layout map could not be drawn: $output"}
    Write-Host "Demo art captured; inspect visually: $output"
} finally {if(-not $process.HasExited){Stop-Process -Id $process.Id;$process.WaitForExit()}}
