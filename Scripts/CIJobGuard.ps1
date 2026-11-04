# Job-started hook for the local self-hosted runner (ACTIONS_RUNNER_HOOK_JOB_STARTED).
# CIRunner.ps1 copies this file into the runner folder (Install and every Start), so a job cannot change it before
# it runs. The repository is public: GitHub only gates fork pull-request workflows behind approval, so this hook is
# the machine-side allowlist. A non-zero exit fails the job before any of its steps run.
$ErrorActionPreference = 'Stop'
$allowedRepository = $env:LD_CI_ALLOWED_REPOSITORY
$allowedEvents = @('schedule', 'workflow_dispatch', 'pull_request')
$repository = $env:GITHUB_REPOSITORY
$eventName = $env:GITHUB_EVENT_NAME
$workflowRef = $env:GITHUB_WORKFLOW_REF
if (-not $allowedRepository) {
    Write-Host 'LD CI guard: LD_CI_ALLOWED_REPOSITORY is not set in the runner .env; refusing every job.'
    exit 1
}
if ($repository -ne $allowedRepository) {
    Write-Host "LD CI guard: refused job from repository '$repository' (allowed: $allowedRepository)."
    exit 1
}
if ($eventName -notin $allowedEvents) {
    Write-Host "LD CI guard: refused '$eventName' job from $workflowRef. This runner only takes $($allowedEvents -join ', ') jobs."
    exit 1
}
if (-not $workflowRef -or -not $workflowRef.StartsWith("$allowedRepository/.github/workflows/")) {
    Write-Host "LD CI guard: refused job with workflow ref '$workflowRef'."
    exit 1
}
if ($eventName -eq 'pull_request') {
    # Only a branch pushed to this repository (which needs write access) may run here; never a fork.
    if (-not $env:GITHUB_EVENT_PATH -or -not (Test-Path -LiteralPath $env:GITHUB_EVENT_PATH)) {
        Write-Host 'LD CI guard: refused pull_request job without a readable event payload.'
        exit 1
    }
    $payload = Get-Content -LiteralPath $env:GITHUB_EVENT_PATH -Raw | ConvertFrom-Json
    $headRepository = $payload.pull_request.head.repo.full_name
    if ($headRepository -ne $allowedRepository) {
        Write-Host "LD CI guard: refused pull request #$($payload.pull_request.number) from '$headRepository'; only branches in $allowedRepository run here."
        exit 1
    }
}
Write-Host "LD CI guard: accepted $eventName job from $workflowRef (actor $env:GITHUB_ACTOR)."
exit 0
