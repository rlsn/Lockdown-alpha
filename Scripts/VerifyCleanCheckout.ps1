[CmdletBinding()]
param([string]$EngineRoot,[ValidateRange(0,16)][int]$MaxParallelActions=0)
. (Join-Path $PSScriptRoot 'Common.ps1')
$engine=Resolve-LDEngine $EngineRoot
$root=Get-LDProjectRoot
if(& git -C $root status --porcelain){throw 'Commit the checkpoint before verifying a clean clone.'}
$output=New-LDArtifactDirectory 'CleanCheckout'
# UBT action paths must also fit when this command runs from a long worktree path.
$checkout=Join-Path (Join-Path $root 'Artifacts') ('cc-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$previousSmudge=$env:GIT_LFS_SKIP_SMUDGE
try {
    $env:GIT_LFS_SKIP_SMUDGE='1'
    & git clone --no-hardlinks -- $root $checkout
    if($LASTEXITCODE -ne 0){throw 'Local clone failed'}
} finally {$env:GIT_LFS_SKIP_SMUDGE=$previousSmudge}
# This lane deliberately hydrates binary content from the audited local LFS
# object cache; this verifies checkout/build reproducibility, not remote LFS delivery.
$lfsEnvironment=& git -C $root lfs env
if($LASTEXITCODE -ne 0){throw 'Cannot discover the Git LFS object cache'}
$mediaDirectories=@($lfsEnvironment | Where-Object {$_ -like 'LocalMediaDir=*'})
if($mediaDirectories.Count -ne 1){throw 'Git LFS did not report one LocalMediaDir'}
$objects=$mediaDirectories[0].Substring('LocalMediaDir='.Length)
if(-not [IO.Path]::IsPathRooted($objects)){$objects=Join-Path $root $objects}
if(-not(Test-Path -LiteralPath $objects -PathType Container)){throw 'Git LFS object cache is missing; hydrate the source checkout first'}
$targetLfs=Join-Path $checkout '.git/lfs'
$targetObjects=Join-Path $targetLfs 'objects'
# Git LFS may already have created the empty objects directory during clone.
# Copy files by relative path so both an empty and an existing cache hydrate safely.
foreach($object in Get-ChildItem -LiteralPath $objects -Recurse -File){
    $destination=Join-Path $targetObjects ([IO.Path]::GetRelativePath($objects,$object.FullName))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $object.FullName -Destination $destination -Force
}
& git -C $checkout lfs fsck
if($LASTEXITCODE -ne 0){throw 'Local LFS object verification failed'}
& git -C $checkout lfs checkout
if($LASTEXITCODE -ne 0){throw 'Local LFS hydration failed'}
if((& git -C $checkout status --porcelain)){throw 'Clone is not clean after LFS hydration'}
foreach($script in @('Preflight','BuildEditor','BuildGame','BootstrapAssets','RunTests')){
    $arguments=@{EngineRoot=$engine}
    if($script -eq 'BootstrapAssets'){$arguments.Mode='Validate'}
    if($script -in @('BuildEditor','BuildGame')){$arguments.MaxParallelActions=$MaxParallelActions}
    & (Join-Path $checkout "Scripts/$script.ps1") @arguments
}
if((& git -C $checkout status --porcelain)){throw 'Verification modified tracked checkout files'}
@{revision=(& git -C $checkout rev-parse HEAD);clean=$true;lfs='Hydrated from local source object cache; remote Git/LFS delivery not tested';checks=@('Preflight','Editor build','Game build','Bootstrap Validate','15 rule tests');checkout=$checkout} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'Result.json')
Write-Host "Clean local clone verification passed. Evidence: $output"
