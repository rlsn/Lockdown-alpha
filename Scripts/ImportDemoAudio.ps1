[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Import','Validate')][string]$Mode='Validate')
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory "DemoAudio-$Mode"
$arguments = @((Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), '-run=LDDemoAudioImport', '-unattended', '-nop4', '-nosound', '-nullrhi')
if ($Mode -eq 'Validate') { $arguments += '-Validate' }
$logPath = Join-Path $output 'DemoAudio.log'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath $logPath
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'LD_DEMO_AUDIO_SUCCESS' -Quiet)) { throw "Demo audio $Mode did not confirm success. Log: $logPath" }
Write-Host "Demo audio $Mode passed. Evidence: $output"
