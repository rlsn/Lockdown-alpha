[CmdletBinding()]
param([string]$EngineRoot,[switch]$Capture,
      [ValidateSet('capital','highfort','scholars','riverport','ironworks','oakmere','millford','reedbank','ashcross','bellwick','stonewake')][string]$Settlement='capital',
      [ValidateRange(60,900)][int]$TimeoutSeconds=300)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
Assert-LDEditorModules
$output=New-LDArtifactDirectory $(if($Capture){"B90Structure-$Settlement-Capture"}else{'B90Structure'})
$arguments='"{0}" /Game/Lockdown/Generated/V09/L_V09Full?Profile=v09_full -game -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -benchmark -UseFixedTimeStep -FPS=60 -ExecCmds="t.MaxFPS 0" -LDB90Structure -B90Settlement={1} -B90Output="{2}" -abslog="{3}"' -f (Join-Path (Get-LDProjectRoot) 'Lockdown.uproject'),$Settlement,$output,(Join-Path $output 'Native.log')
if($Capture){$arguments+=' -RenderOffScreen -windowed -ForceRes -ResX=1600 -ResY=900 -LDB90SettlementCapture'}else{$arguments+=' -nullrhi'}
$process=Start-Process -FilePath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "B90 structure timed out: $output"}
    if($process.ExitCode -ne 0){throw "B90 structure failed ($($process.ExitCode)): $output"}
} finally {if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
& python -B (Join-Path $PSScriptRoot 'CheckB90Structure.py') $output
if($LASTEXITCODE -ne 0){throw "B90 structure evidence failed: $output"}
if($Capture -and -not(Test-Path -LiteralPath (Join-Path $output 'Settlement.png'))){throw "Missing native settlement image: $output"}
Write-Host "B90 structure evidence: $output"
