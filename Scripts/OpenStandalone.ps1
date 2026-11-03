[CmdletBinding()]
param([string]$EngineRoot,[switch]$HostRoom)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
if ($HostRoom) { Assert-LDDemoInputs }
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
$output = New-LDArtifactDirectory 'Standalone'
$world=if($HostRoom){(Get-LDDemoMaps)[1]+'?listen'}else{(Get-LDDemoMaps)[0]}
$arguments = '"{0}" {1} -game -windowed -ResX=1280 -ResY=720 -nosplash -DisablePlugins=AllToolsets,ModelContextProtocol -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), $world, (Join-Path $output 'Standalone.log')
# This command requests a visible interactive game. Editor Python toolsets cannot
# initialize in a -game world; the normal editor retains its MCP plugins.
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Normal
Write-Host "Standalone opened at 1280x720. Evidence: $output"
