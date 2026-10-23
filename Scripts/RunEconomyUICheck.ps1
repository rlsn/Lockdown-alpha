[CmdletBinding()]
param([string]$EngineRoot,[ValidateSet('All','Small','Large','Wide')][string]$Focus='All',[ValidateRange(60,240)][int]$TimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory 'B66-EconomyUI'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
$runStarted=Get-Date
$lanes=@(@{Name='Small';W=1280;H=720;Extra=''},@{Name='Large';W=1280;H=720;Extra='-LDHUDLargeText'},@{Name='Wide';W=1920;H=1080;Extra=''})
if($Focus -ne 'All'){$lanes=@($lanes|Where-Object{$_.Name -eq $Focus})}
foreach($lane in $lanes){
    $folder=Join-Path $output $lane.Name
    New-Item -ItemType Directory -Path $folder|Out-Null
    $arguments='"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -windowed -RenderOffScreen -ForceRes -ResX={1} -ResY={2} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDEconomyUIProbe {3} -LDProbeReportDir="{4}" -abslog="{5}"' -f $project,$lane.W,$lane.H,$lane.Extra,$folder,(Join-Path $folder 'UI.log')
    $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try{if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Native UI $($lane.Name) timed out: $output"};if($process.ExitCode -ne 0){throw "Native UI $($lane.Name) failed ($($process.ExitCode)): $output"}}
    finally{if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    $report=Get-Content -LiteralPath (Join-Path $folder 'UI.json') -Raw|ConvertFrom-Json
    if($report.failures -or @($report.checks).Count -lt 45 -or @(Get-ChildItem -LiteralPath $folder -Filter '*.png').Count -ne 13){throw "Incomplete native UI $($lane.Name) evidence: $output"}
}
$verified=@(Get-ChildItem -Path (Join-Path (Get-LDProjectRoot) 'Saved/Economy/B66Verified*.json')|Where-Object{$_.LastWriteTime -ge $runStarted})
if($verified.Count -ne $lanes.Count){throw "Missing current native UI ledger evidence: $output"}
foreach($file in $verified){Copy-Item -LiteralPath $file.FullName -Destination $output;& python (Join-Path $PSScriptRoot 'CheckEconomyEvidence.py') (Join-Path $output $file.Name);if($LASTEXITCODE -ne 0){throw "Native UI independent replay failed: $output"}}
@{sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);lanes=@($lanes|ForEach-Object{$_.Name});display=(Get-LDDisplayEnvironment);scope='Native offscreen UMG/Slate and actual Enhanced Input; two local owners; diagnostic resource/progression/public-window fixtures; no human, remote or OS-scale acceptance'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $output 'Summary.json') -Encoding utf8
Write-Host "B66 native UI checks passed. Inspect captures: $output"
