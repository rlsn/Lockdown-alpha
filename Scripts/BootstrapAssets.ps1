[CmdletBinding()]
param([string]$EngineRoot, [ValidateSet('Generate','Validate','Regenerate','RefreshDefinitions')][string]$Mode='Validate')
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output = New-LDArtifactDirectory "Bootstrap-$Mode"
$arguments = @((Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'), '-run=LDScaffoldBootstrap', '-unattended', '-nop4', '-nosound', '-nullrhi', "-Output=$output")
if ($Mode -ne 'Generate') { $arguments += "-$Mode" }
$logPath = Join-Path $output 'Bootstrap.log'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath $logPath
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'LD_DEMO_BOOTSTRAP_SUCCESS' -Quiet)) { throw "Demo bootstrap did not confirm success. Log: $logPath" }
Write-Host "Bootstrap $Mode passed. Evidence: $output"
