[CmdletBinding()]
param([string]$EngineRoot,[switch]$DevContentReload)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
# Invoking OpenEditor requests an interactive editor window.
# -DevContentReload: every new PIE match plays the demo_content.json last written by SyncDemoContentData.py, without
# rebuilding C++ or restarting the editor (-LDDevContentReload). Only those numbers reload; the room plays only the
# Host and AI.
$reload = if ($DevContentReload) { ' -LDDevContentReload' } else { '' }
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList ('"{0}"{1}' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), $reload) -WindowStyle Normal
