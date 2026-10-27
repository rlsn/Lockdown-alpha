[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(0,16)][int]$MaxParallelActions=0,[switch]$Gather,[switch]$FullUnity)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
$output = New-LDArtifactDirectory 'BuildEditor'
$project = Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'
$buildArguments=@('LockdownEditor', 'Win64', 'Development', "-Project=$project", '-WaitMutex', '-NoHotReloadFromIDE', '-Unattended')
if($Gather){$buildArguments+='-gather'}
if($FullUnity){$buildArguments+='-DisableAdaptiveUnity'}
if($MaxParallelActions){$buildArguments+="-MaxParallelActions=$MaxParallelActions"}
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Build/BatchFiles/Build.bat') -ToolArguments $buildArguments -LogPath (Join-Path $output 'BuildEditor.log')
Write-Host "Editor build passed. Evidence: $output"
