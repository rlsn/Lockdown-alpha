[CmdletBinding()]
param([string]$EngineRoot,[switch]$Listen,[string]$Connect,[switch]$Visible)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
if($Connect -and $Listen){throw 'Choose either a listen host or a connection'}
& python -B (Join-Path $PSScriptRoot 'SyncB91Data.py') --check
if($LASTEXITCODE -ne 0){throw 'B91 contract is stale'}
$destination=if($Connect){$Connect}else{'/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB91GameMode?Profile=v09_environment'}
if($Listen){$destination+='?listen'}
$arguments='"{0}" "{1}" -game -windowed -ResX=1600 -ResY=900 -DisablePlugins=AllToolsets,ModelContextProtocol' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$destination
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle $(if($Visible){'Normal'}else{'Hidden'})
