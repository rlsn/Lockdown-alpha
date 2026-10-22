[CmdletBinding()]
param([string]$EngineRoot,[string]$PackageRoot,[switch]$Region=$true,[ValidateSet(0,100,150)][int]$RoundTripMs=0,[ValidateRange(0,25)][int]$JitterMs=0,[ValidateRange(-1,100)][int]$OneWayBaseMs=-1,[ValidateRange(1024,65000)][int]$Port=7803)
. (Join-Path $PSScriptRoot 'Common.ps1')
if(-not $Region){throw 'Vendor earned-route verification requires the regional Guild map; use the default Region profile.'}
if($PackageRoot){$package=Resolve-LDPackage $PackageRoot;$binary=$package.Binary;$project='';$lane='Packaged-'}
else{$engine=Resolve-LDEngine $EngineRoot;Assert-LDEditorModules;$binary=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$project=Join-Path (Get-LDProjectRoot) 'Lockdown.uproject';$lane=''}
$output=New-LDArtifactDirectory ($lane+'Vendor-'+$RoundTripMs+'-'+$JitterMs)
if($OneWayBaseMs -lt 0){$OneWayBaseMs=[int]($RoundTripMs/2)}
$minimum=[Math]::Max(0,$OneWayBaseMs-$JitterMs);$maximum=$OneWayBaseMs+$JitterMs
@{sourceRevision=(git rev-parse HEAD);sourceDirty=[bool](git status --porcelain);profile='V05 B55 quoted vendor with one normal earned route and labelled adversarial fixtures';region=[bool]$Region;targetRTT=$RoundTripMs;oneWayBaseMs=$OneWayBaseMs;jitterMs=$JitterMs;frameCap=60;topology='Separate authority and two independent owning clients; headless listen authority for packaged lane'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'Environment.json') -Encoding utf8
$ownedProcesses=@()
try {
    foreach($role in @('Server','A','B')) {
        $world=if($role -ne 'Server'){"127.0.0.1:$Port"}elseif($Region){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?Profile=V05'}else{'/Game/Lockdown/Generated/Maps/L_MigrationSandbox?Profile=V05'}
        $mode=if($role -eq 'Server'){'-server'}else{'-game'}
        if($PackageRoot -and $role -eq 'Server'){$world+='?listen';$mode='-game -LDRemoteAuthority'}
        $arguments='"{0}" {1} {2} -port={3} -nullrhi -unattended -nosound -nop4 -DisablePlugins=AllToolsets,ModelContextProtocol -ExecCmds="t.MaxFPS 60" -LDVendorProbe -PktLagMin={4} -PktLagMax={5} -LDProbeRole={6} -LDProbeReportDir="{7}" -abslog="{8}"' -f $project,$world,$mode,$Port,$minimum,$maximum,$role,$output,(Join-Path $output "$role.log")
        if($PackageRoot){$arguments=$arguments.Substring(3)}
        $ownedProcesses+=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if($role -eq 'Server'){Start-Sleep -Seconds 3}
    }
    $deadline=(Get-Date).AddSeconds(480)
    while(@($ownedProcesses|Where-Object{-not $_.HasExited}).Count -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 500}
    foreach($process in $ownedProcesses){if(-not $process.HasExited){throw "Vendor probe timed out: $output"};if($process.ExitCode -ne 0){throw "Vendor probe failed ($($process.ExitCode)): $output"}}
    foreach($role in @('Server','A','B')) {
        $report=Get-Content -LiteralPath (Join-Path $output "$role.json") -Raw|ConvertFrom-Json
        $required=if($role -eq 'Server'){23}elseif($role -eq 'A'){19}else{14}
        if($report.failures -ne 0 -or @($report.checks).Count -lt $required){throw "Failed or incomplete $role Vendor evidence: $output"}
        Write-Host "$role passed $(@($report.checks).Count) checks"
    }
    Write-Host "Vendor verification passed. Evidence: $output"
} finally {
    foreach($process in $ownedProcesses){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}}
}
