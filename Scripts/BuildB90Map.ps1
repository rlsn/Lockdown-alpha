[CmdletBinding()]
param([string]$EngineRoot,[switch]$Validate,[switch]$PresentationOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
if($Validate -and $PresentationOnly){throw 'Validate cannot modify presentation.'}
$output = New-LDArtifactDirectory 'B90Map'
& python -B (Join-Path $PSScriptRoot 'SyncV09Data.py') --check
if($LASTEXITCODE -ne 0){throw 'B90 compiled map is stale.'}
$arguments = @((Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),'-run=LDB90Bootstrap','-unattended','-nop4','-nosound','-nullrhi',"-Output=$output")
if($Validate){$arguments+='-Validate'}
if($PresentationOnly){$arguments+='-PresentationOnly'}
$log = Join-Path $output 'Bootstrap.log'
Invoke-LDTool -Tool (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath $log
if(-not (Select-String -LiteralPath $log -SimpleMatch 'LD_B90_SUCCESS' -Quiet)){throw "B90 builder did not finish: $log"}
Write-Host "B90 native map evidence: $output"
