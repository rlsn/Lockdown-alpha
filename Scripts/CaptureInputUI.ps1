[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[switch]$Controls,[ValidateSet('HUD','Controls','Lobby','Selected','Death','Result','Inventory','InventoryDead','Vendor','VendorFull','VendorFailed','QuickItems','ItemReading','DropConfirm')][string]$Scene='HUD',[switch]$LargeText,[ValidateRange(1280,2560)][int]$Width=1280,[ValidateRange(720,1440)][int]$Height=720)
. (Join-Path $PSScriptRoot 'Common.ps1')
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$project='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe';$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject';$lane=''}
if($Controls){$Scene='Controls'}
$output=New-LDArtifactDirectory ($lane+'InputUI-'+$Width+'x'+$Height+'-'+$Scene+$(if($LargeText){'-125pct'}else{''}))
$extra='-LDHUDScene='+$Scene+$(if($Scene -eq 'Controls'){' -LDControlsVisual'}else{''})+$(if($LargeText){' -LDHUDLargeText'}else{''})
$arguments='"{0}" /Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05 -game -windowed -RenderOffScreen -ForceRes -ResX={1} -ResY={2} -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -LDDevelopmentProbe -LDRegionProbe -LDProbeRole=Visual {3} -LDProbeReportDir="{4}" -abslog="{5}"' -f $project,$Width,$Height,$extra,$output,(Join-Path $output 'Visual.log')
if($PackageRoot){$arguments=$arguments.Substring(3)}
@{sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);width=$Width;height=$Height;scene=$Scene;largeText=[bool]$LargeText;method='Native offscreen rendered UI; auto-start/location/death/deadline fixtures as selected; not human or OS-scale acceptance'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
$process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit(150000)){throw "Native UI capture timed out: $output"}
    if($process.ExitCode -ne 0){throw "Native UI capture failed: $output"}
    $report=Get-Content -LiteralPath (Join-Path $output 'Visual.json') -Raw|ConvertFrom-Json
    $image=Join-Path $output ($Scene+'.png')
    if($report.failures -ne 0 -or -not (Test-Path -LiteralPath $image)){throw "Missing or failed UI evidence: $output"}
    Write-Host "Native UI captured; inspect visually: $image"
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
