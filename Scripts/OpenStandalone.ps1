[CmdletBinding()]
param([string]$EngineRoot,[switch]$NavigationCheckpoint,[ValidateSet("v06_regression","v07_integration")][string]$Profile)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory 'Standalone'
$world=if($Profile -eq "v07_integration"){"/Game/Lockdown/Generated/Maps/L_V07Region?listen?Profile=$Profile"}elseif($Profile){"/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?listen?Profile=$Profile"}elseif($NavigationCheckpoint){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?listen?Profile=V05'}else{''}
$arguments = '"{0}" {1} -game -windowed -ResX=1280 -ResY=720 -nosplash -DisablePlugins=AllToolsets,ModelContextProtocol -abslog="{2}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), $world, (Join-Path $output 'Standalone.log')
# This command requests a visible interactive game. Editor Python toolsets cannot
# initialize in a -game world; the normal editor retains its MCP plugins.
Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Normal
Write-Host "Standalone opened at 1280x720. Evidence: $output"
