[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
# Invoking OpenEditor requests an interactive editor window.
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList ('"{0}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject')) -WindowStyle Normal
