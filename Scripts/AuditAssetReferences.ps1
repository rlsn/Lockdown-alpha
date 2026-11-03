[CmdletBinding()]
param(
    [string]$EngineRoot,
    [Parameter(Mandatory)][string]$RequestPath,
    [switch]$AllowMissingRoots
)
. (Join-Path $PSScriptRoot 'Common.ps1')
$request = (Resolve-Path -LiteralPath $RequestPath).Path
$engine = Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
Set-LDPlatformValidation Validate
$output = New-LDArtifactDirectory 'AssetReferenceAudit'
$arguments = @(
    (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),
    '-run=LDAssetReferenceAudit',
    "-Request=$request",
    "-Report=$(Join-Path $output 'AssetReferences.json')",
    '-unattended', '-nop4', '-nosound', '-nullrhi'
)
if ($AllowMissingRoots) { $arguments += '-AllowMissingRoots' }
Invoke-LDTool -Tool (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ToolArguments $arguments -LogPath (Join-Path $output 'Audit.log')
Write-Host "Read-only asset reference evidence: $output"
