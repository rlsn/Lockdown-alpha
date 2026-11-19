[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateRange(30,180)][int]$TimeoutSeconds=140)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output=New-LDArtifactDirectory 'DemoSolo-CameraAndVisibility'
$launch=Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$report=Join-Path $launch.ProbeOutput 'Solo.json'
$log=Join-Path $output 'Solo.log'
$arguments='-unattended -nop4 -nosound -windowed -RenderOffScreen -ResX=1280 -ResY=720 -LDDemoSoloProbe -LDDemoSoloOutput="{0}" -abslog="{1}"' -f $report,$log
$process=$null
try {
    $process=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url '/Game/Lockdown/Generated/Maps/L_Demo?Name=SoloCameraCheck' -Arguments $arguments) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Solo flow timed out: $output"}
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $report)){throw "Solo process failed: $output"}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if(-not $r.passed -or @($r.checks|Where-Object {-not $_.passed}).Count){throw "Solo flow failed: $($r.error). Evidence: $output"}
    $errors=@(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|LogLD\w+: Error:|LD_DEMO_AUTHORITY_FAULT')
    if($errors.Count){throw "Solo flow logged an error: $($errors[0].Line). Evidence: $output"}
    Write-Host "Solo human / zero AI camera and visibility checks passed: $output"
} finally {
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
    Complete-LDGameEvidence $launch
}
