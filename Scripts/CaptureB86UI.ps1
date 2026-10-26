[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateSet(1280,1920)][int]$Width=1280,[switch]$LargeText)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$prefix='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$prefix='"{0}" ' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject');$lane='Editor-'}
$height=if($Width -eq 1920){1080}else{720}
$output=New-LDArtifactDirectory ($lane+"B86-UI-$Width"+$(if($LargeText){'-125pct'}else{''}))
$extra=if($LargeText){'-LDB86LargeText'}else{''}
$arguments=$prefix+('/Game/Lockdown/Generated/Maps/L_V08Region?Profile=v08_integration -game -windowed -RenderOffScreen -ForceRes -ResX={0} -ResY={1} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDB86Probe -LDProbeRole=Visual {2} -LDProbeReportDir="{3}" -abslog="{4}"' -f $Width,$height,$extra,$output,(Join-Path $output 'Visual.log'))
@{display=(Get-LDDisplayEnvironment);sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);width=$Width;height=$height;largeText=[bool]$LargeText;method='Native offscreen rendered UMG, native Enhanced Input; labelled stats/knowledge/items/placement fixture, real held preparation. Not human or OS-input acceptance.'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Environment.json')
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(180000)){throw "B86 visual fixture timeout: $output"}
    if($process.ExitCode -ne 0){throw "B86 visual fixture failed: $output"}
    $report=Get-Content -LiteralPath (Join-Path $output 'Visual.json') -Raw|ConvertFrom-Json
    if($report.failures -ne 0 -or @($report.checks).Count -lt 15){throw "Missing B86 native UI assertions: $output"}
    foreach($scene in @('HUD','Inventory','Character','Controls')){if(-not(Test-Path -LiteralPath (Join-Path $output "$scene.png"))){throw "Missing rendered $scene screenshot"}}
    Write-Host "B86 native UI passed $(@($report.checks).Count) checks. Inspect screenshots: $output"
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
