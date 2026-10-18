[CmdletBinding()]
param([Parameter(Mandatory)][string]$BaselineRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
$baseline = (Resolve-Path -LiteralPath $BaselineRoot).Path
$loader = Join-Path $baseline 'node_modules/tsx/dist/loader.mjs'
if (-not (Test-Path -LiteralPath $loader)) { throw 'Baseline dependencies are missing. Install its locked dependencies separately before exporting.' }
$status = & git -C $baseline status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect baseline git status.' }
if ($status) { throw 'Baseline has local changes; commit or isolate them before pinning a revision.' }
$output = New-LDArtifactDirectory 'BaselineExport'
Invoke-LDTool -Tool 'node' -ToolArguments @('--import', ([uri]$loader).AbsoluteUri, (Join-Path $PSScriptRoot 'ExportBaseline.mjs'), $baseline, (Join-Path (Get-LDProjectRoot) 'Config/BaselineSnapshot.json')) -LogPath (Join-Path $output 'Export.log')
