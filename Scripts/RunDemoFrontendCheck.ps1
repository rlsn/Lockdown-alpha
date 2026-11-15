[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(60,300)][int]$TimeoutSeconds=180,[switch]$LobbyOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Skip
$output=New-LDArtifactDirectory $(if($LobbyOnly){'DemoLobbySettings-Rendered'}else{'DemoFrontend-Rendered'})
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$map=if($LobbyOnly){'/Game/Lockdown/Generated/Maps/L_Demo?listen?LDName=LobbySettings'}else{'/Game/Lockdown/Generated/Maps/L_Frontend'}
$probe=if($LobbyOnly){'-LDDemoLobbyUIProbe'}else{'-LDDemoFrontendUIProbe'}
$arguments='"{0}" "{4}" -game -nop4 -nosound -windowed -RenderOffScreen -ResX=1280 -ResY=720 -DisablePlugins=AllToolsets,ModelContextProtocol,Fab -UserDir="{1}" {5} -LDDemoFrontendUIOutput="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$user,$output,(Join-Path $output 'Frontend.log'),$map,$probe
$process=Start-Process -FilePath (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $process.Id;throw "Rendered frontend check timed out: $output"}
$reportPath=Join-Path $output 'FrontendSettings.json'
if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $reportPath)){throw "Rendered frontend check failed: $output"}
$report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
if(-not $report.passed -or $report.rhi -in @('None','Null') -or @($report.checks|Where-Object{-not $_.passed}).Count){throw "Incomplete frontend UI evidence: $reportPath"}
$images=if($LobbyOnly){@('Lobby-Initial.png','Lobby-SettingsEntry.png','Lobby-Settings.png','Lobby-Returned.png')}else{@('Frontend-100.png','Settings-Display-100.png','Settings-Audio-100.png','Settings-Controls-100.png','Settings-Camera-100.png','Settings-Interface-100.png','Settings-Display-125.png','Settings-Audio-125.png','Settings-Controls-125.png','Settings-Camera-125.png','Settings-Interface-125.png','DiscardDisplayDraft.png','Settings-RebindConflict.png','Settings-ResetConfirm.png')}
foreach($name in $images){
    $path=Join-Path $output $name
    if(-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -eq 0){throw "Missing real rendered screenshot: $path"}
}
Write-Host "Rendered native frontend/settings fixture passed without changing display modes. Evidence: $output"
