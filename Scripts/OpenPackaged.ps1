[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot,[ValidateSet('720p','1080p')][string]$Resolution='720p')
. (Join-Path $PSScriptRoot 'Common.ps1')
$package=Resolve-LDPackage $PackageRoot
$output=New-LDArtifactDirectory 'Packaged-Interactive'
$size=if($Resolution -eq '720p'){'-ResX=1280 -ResY=720'}else{'-ResX=1920 -ResY=1080'}
# Explicitly requested visible interactive game; use native Host/Join buttons.
Start-Process -FilePath $package.Binary -ArgumentList ('-windowed {0} -abslog="{1}"' -f $size,(Join-Path $output 'Game.log')) -WindowStyle Normal
Write-Host "Packaged frontend opened. Evidence: $output"
