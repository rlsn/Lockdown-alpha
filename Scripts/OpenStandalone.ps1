[CmdletBinding()]
param([string]$EngineRoot,[switch]$HostRoom,[switch]$Windowed)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
if ($HostRoom) { Assert-LDDemoInputs }
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
$output = New-LDArtifactDirectory 'Standalone'
$world=if($HostRoom){(Get-LDDemoMaps)[1]+'?listen'}else{(Get-LDDemoMaps)[0]}
# The game always runs borderless at the desktop resolution. -Windowed is only for several clients on one machine;
# a window cannot edge-pan once the pointer leaves it.
$window = if ($Windowed) { '-windowed -ResX=1280 -ResY=720' } else { '' }
$arguments = '"{0}" {1} -game {3} -nosplash -DisablePlugins=AllToolsets,ModelContextProtocol -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), $world, (Join-Path $output 'Standalone.log'), $window
# This command requests a visible interactive game. Editor Python toolsets cannot
# initialize in a -game world; the normal editor retains its MCP plugins.
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Normal
Write-Host ("Standalone opened {0}. Evidence: $output" -f $(if ($Windowed) { "in a 1280x720 window" } else { "borderless" }))
