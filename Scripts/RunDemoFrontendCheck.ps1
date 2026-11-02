[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'DemoFrontend-Rendered'
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$arguments='"{0}" /Game/Lockdown/Generated/Maps/L_Frontend -game -nop4 -nosound -windowed -RenderOffScreen -ResX=1280 -ResY=720 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{1}" -LDDemoFrontendUIProbe -LDDemoFrontendUIOutput="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$user,$output,(Join-Path $output 'Frontend.log')
$process=Start-Process -FilePath (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $process.Id;throw "Rendered frontend check timed out: $output"}
$reportPath=Join-Path $output 'FrontendSettings.json'
if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $reportPath)){throw "Rendered frontend check failed: $output"}
$report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
if(-not $report.passed -or $report.rhi -in @('None','Null') -or @($report.checks|Where-Object{-not $_.passed}).Count){throw "Incomplete frontend UI evidence: $reportPath"}
foreach($name in @('Frontend-100.png','Settings-100.png','Settings-125.png','DiscardDisplayDraft.png')){
    $path=Join-Path $output $name
    if(-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -eq 0){throw "Missing real rendered screenshot: $path"}
}
Write-Host "Rendered native frontend/settings fixture passed without changing display modes. Evidence: $output"
