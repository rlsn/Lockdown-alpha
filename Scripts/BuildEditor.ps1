[CmdletBinding()]
param([string]$EngineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
$output = New-LDArtifactDirectory 'BuildEditor'
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/Build.bat') -ToolArguments @('LockdownEditor', 'Win64', 'Development', "-Project=$project", '-WaitMutex', '-NoHotReloadFromIDE', '-Unattended') -LogPath (Join-Path $output 'BuildEditor.log')
Write-Host "Editor build passed. Evidence: $output"
