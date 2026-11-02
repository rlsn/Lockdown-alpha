[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot,[ValidateSet('720p','1080p')][string]$Resolution='720p')
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'DemoEntry.ps1')
$package=Resolve-LDPackage $PackageRoot
Assert-LDDemoPackage $package
$output=New-LDArtifactDirectory 'Packaged-Interactive'
$size=if($Resolution -eq '720p'){'-ResX=1280 -ResY=720'}else{'-ResX=1920 -ResY=1080'}
# Explicitly requested visible interactive game; use native Host/Join buttons.
$world=(Get-LDDemoMaps)[0]
Start-Process -FilePath $package.Binary -ArgumentList ('{2} -windowed {0} -abslog="{1}"' -f $size,(Join-Path $output 'Game.log'),$world) -WindowStyle Normal
Write-Host "Packaged frontend opened. Evidence: $output"
