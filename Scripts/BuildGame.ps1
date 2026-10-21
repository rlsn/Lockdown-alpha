[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(0,16)][int]$MaxParallelActions=0)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
$output=New-LDArtifactDirectory 'BuildGame'
$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$buildArguments=@('Lockdown','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE','-Unattended')
if($MaxParallelActions){$buildArguments+="-MaxParallelActions=$MaxParallelActions"}
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/Build.bat') -ToolArguments $buildArguments -LogPath (Join-Path $output 'BuildGame.log')
Write-Host "Game target built. Cooking/packaging and network acceptance remain separate: $output"
