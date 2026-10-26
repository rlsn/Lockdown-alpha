[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot,[ValidateSet('720p','1080p')][string]$Resolution='720p',[ValidateSet('v06_regression','v07_integration','v08_integration')][string]$Profile)
. (Join-Path $PSScriptRoot 'Common.ps1')
$package=Resolve-LDPackage $PackageRoot
$output=New-LDArtifactDirectory 'Packaged-Interactive'
$size=if($Resolution -eq '720p'){'-ResX=1280 -ResY=720'}else{'-ResX=1920 -ResY=1080'}
# Explicitly requested visible interactive game; use native Host/Join buttons.
$world=if($Profile -eq 'v08_integration'){'/Game/Lockdown/Generated/Maps/L_V08Region?listen?Profile=v08_integration'}elseif($Profile -eq 'v07_integration'){'/Game/Lockdown/Generated/Maps/L_V07Region?listen?Profile=v07_integration'}elseif($Profile){'/Game/Lockdown/Generated/Maps/L_DevelopmentRegion?listen?Profile=v06_regression'}else{''}
Start-Process -FilePath $package.Binary -ArgumentList ('{2} -windowed {0} -abslog="{1}"' -f $size,(Join-Path $output 'Game.log'),$world) -WindowStyle Normal
Write-Host "Packaged frontend opened. Evidence: $output"
