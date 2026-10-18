[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
$output=New-LDArtifactDirectory 'BuildGame'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/Build.bat') -ToolArguments @('Lockdown','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE','-Unattended') -LogPath (Join-Path $output 'BuildGame.log')
Write-Host "Game target built. Cooking/packaging and network acceptance remain separate: $output"
