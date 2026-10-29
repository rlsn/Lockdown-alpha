[CmdletBinding()]
param([string]$EngineRoot,[switch]$StopOnFailure,[ValidateRange(1,8)][int]$MaxParallelActions=2)
. (Join-Path $PSScriptRoot 'Common.ps1')
$completionOutput=New-LDArtifactDirectory 'B91-CompletionRun'
$completionRuns=@{}
$completionResults=@()
function Invoke-CompletionCase([string]$Name,[string]$Script,[hashtable]$Options=@{}){
    $before=@(Get-ChildItem (Join-Path (Get-LDProjectRoot) 'Artifacts') -Directory | Select-Object -ExpandProperty FullName)
    $passed=$true;$detail=''
    try{if($EngineRoot){$Options.EngineRoot=$EngineRoot};& (Join-Path $PSScriptRoot ($Script+'.ps1')) @Options}catch{$passed=$false;$detail="$($_)";Write-Host "B91 final case $Name failed: $detail"}
    $created=@(Get-ChildItem (Join-Path (Get-LDProjectRoot) 'Artifacts') -Directory | Where-Object {$_.FullName -notin $before} | Sort-Object Name)
    if($created.Count -ne 1){$passed=$false;$detail+=' Expected one artifact directory.'}else{$completionRuns[$Name]=$created[0].FullName}
    $script:completionResults+=@{case=$Name;passed=$passed;detail=$detail}
    $completionRuns | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $completionOutput 'Runs.json') -Encoding utf8
    $completionResults | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $completionOutput 'Results.json') -Encoding utf8
    if($StopOnFailure -and -not $passed){throw "B91 case $Name failed: $completionOutput"}
}
Invoke-CompletionCase 'editor' 'BuildEditor' @{MaxParallelActions=$MaxParallelActions;FullUnity=$true}
if(-not $completionResults[-1].passed){throw "B91 editor build failed: $completionOutput"}
Invoke-CompletionCase 'rules' 'RunTests'
Invoke-CompletionCase 'combat' 'RunB91Combat'
Invoke-CompletionCase 'encounter' 'RunB91Encounter' @{Capture=$true}
Invoke-CompletionCase 'fit_a' 'RunB91Forest' @{Fit=$true}
Invoke-CompletionCase 'fit_b' 'RunB91Forest' @{Fit=$true}
Invoke-CompletionCase 'forest' 'RunB91Forest'
Invoke-CompletionCase 'routes' 'RunB91ForestRoutes'
Invoke-CompletionCase 'structural' 'RunB91Check'
Invoke-CompletionCase 'doors' 'RunB91Network'
Invoke-CompletionCase 'objects' 'RunB91Network' @{Objects=$true}
Invoke-CompletionCase 'furniture' 'RunB91Network' @{Furniture=$true}
Invoke-CompletionCase 'tools' 'RunB91Network' @{Tools=$true}
Invoke-CompletionCase 'traversal' 'RunB91Network' @{Traversal=$true}
Invoke-CompletionCase 'saplings' 'RunB91Network' @{Saplings=$true}
Invoke-CompletionCase 'fields' 'RunB91Network' @{Fields=$true}
Invoke-CompletionCase 'ignition' 'RunB91Network' @{Ignition=$true}
Invoke-CompletionCase 'grass' 'RunB91Network' @{Grass=$true}
Invoke-CompletionCase 'checkpoint' 'RunB91Checkpoint' @{TimeoutSeconds=300}
Invoke-CompletionCase 'legacy' 'RunB84Check'
Invoke-CompletionCase 'game' 'BuildGame' @{MaxParallelActions=$MaxParallelActions;FullUnity=$true}
if(@($completionResults | Where-Object {-not $_.passed}).Count){throw "B91 final matrix has failures: $completionOutput"}
& python -B (Join-Path $PSScriptRoot 'ExportB91CompletionEvidence.py') (Join-Path $completionOutput 'Runs.json') --check
if($LASTEXITCODE -ne 0){throw "B91 final evidence rejected: $completionOutput"}
Write-Host "PASS B91 completion matrix: $completionOutput"
