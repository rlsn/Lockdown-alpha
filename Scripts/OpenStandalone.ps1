[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'Standalone'
$arguments = '"{0}" -game -windowed -ResX=1280 -ResY=720 -nosplash -DisablePlugins=AllToolsets,ModelContextProtocol -abslog="{1}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), (Join-Path $output 'Standalone.log')
# This command requests a visible interactive game. Editor Python toolsets cannot
# initialize in a -game world; the normal editor retains its MCP plugins.
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Normal
Write-Host "Standalone opened at 1280x720. Evidence: $output"
