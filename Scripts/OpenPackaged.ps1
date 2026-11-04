[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot,[switch]$Windowed,[ValidateSet('720p','1080p')][string]$Resolution='720p')
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
$package=Resolve-LDPackage $PackageRoot
Assert-LDDemoPackage $package
$output=New-LDArtifactDirectory 'Packaged-Interactive'
# Borderless at the desktop resolution unless -Windowed (several clients on one machine); -Resolution sizes that window.
$window=if(-not $Windowed){''}elseif($Resolution -eq '720p'){'-windowed -ResX=1280 -ResY=720'}else{'-windowed -ResX=1920 -ResY=1080'}
# Explicitly requested visible interactive game; use native Host/Join buttons.
$world=(Get-LDDemoMaps)[0]
Start-Process -FilePath $package.Binary -ArgumentList ('{2} {0} -abslog="{1}"' -f $window,(Join-Path $output 'Game.log'),$world) -WindowStyle Normal
Write-Host "Packaged frontend opened. Evidence: $output"
