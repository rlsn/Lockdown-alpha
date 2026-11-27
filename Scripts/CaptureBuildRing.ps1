[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[ValidateRange(120,900)][int]$TimeoutSeconds=600)
# Rendered review captures of the build ring (构筑气场环) in a solo DEV room: the open ring in the field (offer row, hover
# links, hand selection, stat preview, medal socket), the closed state, the opening keyframes and, in the first
# mid-match rest, the shop wing, at 1920x1080 and 1280x720 with 100% and 125% text. Presentation evidence only; the
# images are for a person to look at, the report only proves the run reached every shot.
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
Assert-LDDemoInputs
$output=New-LDArtifactDirectory 'BuildRing-Capture'
$launch=Get-LDGameLaunch -EngineRoot $EngineRoot -PackageRoot $PackageRoot -Output $output
$log=Join-Path $output 'BuildRing.log'
$user=Join-Path $output 'User'
New-Item -ItemType Directory -Path $user | Out-Null
$arguments='-unattended -nop4 -nosound -windowed -RenderOffScreen -ForceRes -ResX=1920 -ResY=1080 -UserDir="{0}" -LDDevGrowthFixture -LDBuildRingProbe -LDBuildRingOutput="{1}" -abslog="{2}"' -f $user,$launch.ProbeOutput,$log
$process=$null
try {
    $process=Start-Process -FilePath $launch.Binary -ArgumentList (Format-LDGameArguments -Launch $launch -Url '/Game/Lockdown/Generated/Maps/L_Demo?Name=BuildRingCapture' -Arguments $arguments) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){throw "Build ring capture timed out: $output"}
    Complete-LDGameEvidence $launch
    $report=Join-Path $output 'BuildRing.json'
    if(-not (Test-Path -LiteralPath $report)){throw "Build ring capture wrote no report: $output"}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if(-not $r.passed){throw "Build ring capture failed: $($r.error). Evidence: $output"}
    $errors=@(Select-String -LiteralPath $log -Pattern 'Fatal error|Assertion failed|LD_DEMO_AUTHORITY_FAULT')
    if($errors.Count){throw "Build ring capture logged an error: $($errors[0].Line). Evidence: $output"}
    Write-Host "Build ring captured ($(@($r.screenshots).Count) shots); inspect visually: $output"
} finally {
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id}
}
