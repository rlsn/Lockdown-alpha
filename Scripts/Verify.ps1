# Dependency-scoped local checks; -Plan explains selection without running checks.
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$Base='origin/main',
    [string[]]$Filter,
    [ValidateSet('None','Auto')][string]$Rules='None',
    [switch]$Plan,
    [switch]$Force,
    [string[]]$Paths,
    [switch]$Json
)
# UTF-8 for this process and every child it starts, whatever the system locale (a zh-CN console defaults to GBK).
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$ErrorActionPreference = 'Stop'
if ($Paths -and -not $Plan) { throw '-Paths is a simulation option and requires -Plan.' }
if ($Json -and -not $Plan) { throw '-Json requires -Plan.' }
$arguments = @('--base', $Base, '--rules', $Rules)
if ($EngineRoot) { $arguments += @('--engine', $EngineRoot) }
foreach ($prefix in $Filter) { $arguments += @('--filter', $prefix) }
foreach ($path in $Paths) { $arguments += @('--path', $path) }
if ($Plan) { $arguments += '--plan' }
if ($Force) { $arguments += '--force' }
if ($Json) { $arguments += '--json' }
Push-Location (Join-Path $PSScriptRoot '..')
try {
    & python -B (Join-Path $PSScriptRoot 'LocalVerification.py') @arguments
    if ($LASTEXITCODE -ne 0) { throw "Local verification failed ($LASTEXITCODE)." }
} finally {
    Pop-Location
}
