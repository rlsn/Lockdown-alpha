[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateRange(120,900)][int]$TimeoutSeconds=600)
# Rendered review captures of the vector cast in a solo DEV room (Probes/LDVectorCastProbe.h): the cordon tape and the desk
# fan pressed at a ground start, dragged along several angles (and tapped), each with its held preview and its placed shape,
# through the product's own input, request and server path. The log's LD_COMBAT_CORDON_PLACED / LD_COMBAT_FAN_PLACED lines
# carry the placed geometry; the images are for a person to look at.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output=New-LDArtifactDirectory 'VectorCast-Capture'
$launch=Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$log=Join-Path $output 'VectorCast.log'
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$arguments='-unattended -nop4 -nosound -windowed -RenderOffScreen -ForceRes -ResX=1920 -ResY=1080 -UserDir="{0}" -LDDevGrowthFixture -LDVectorCastProbe -LDVectorCastOutput="{1}" -abslog="{2}"' -f $user,$launch.ProbeOutput,$log
$process=$null
try {
    $process=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url '/Game/Lockdown/Generated/Maps/L_Demo?Name=VectorCastCapture' -Arguments $arguments) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Vector cast capture timed out: $output"}
    Complete-LDGameEvidence $launch
    $report=Join-Path $output 'VectorCast.json'
    if(-not (Test-Path -LiteralPath $report)){throw "Vector cast capture wrote no report: $output"}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if(-not $r.passed){throw "Vector cast capture failed: $($r.error). Evidence: $output"}
    $errors=@(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|LD_DEMO_AUTHORITY_FAULT')
    if($errors.Count){throw "Vector cast capture logged an error: $($errors[0].Line). Evidence: $output"}
    Write-Host "Vector cast captured ($(@($r.screenshots).Count) shots); inspect visually: $output"
} finally {
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
}
