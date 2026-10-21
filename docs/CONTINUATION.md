# Continuation — v0.5 ready for implementation

21 October 2026. The user supplied master plan revision 5 and the v0.5 UI/control
specification and requested review, commit and preparation for implementation.
The review is recorded in [B50 baseline review](v05/B50_BASELINE_REVIEW.md).
No v0.5 runtime changes have started. Begin B51 native multiplayer navigation next.

## Read first

1. [Master plan revision 5](master/README.md), especially UI/controls and privacy.
2. [v0.5 specification](v05/README.md), all chapters, JSON and wireframes.
3. [Actual baseline and adopted defaults](v05/B50_BASELINE_REVIEW.md) and
   [working feature ledger](v05/FeatureLedger.csv).
4. [B50–B58 build plan](v05/08_BUILD_PLAN.md),
   [U01–U56 acceptance](v05/09_ACCEPTANCE_AND_USER_REVIEW.md) and
   [build handoff](v05/10_CODEX_BUILD_PROMPT.md).
5. [Archived B47 continuation](v04/CONTINUATION_B47.md) and
   [B47 engineering evidence](v04/B47_IMPLEMENTATION.md).

## Baseline and scope

Handoff revision: `b8f003a6535079c98dd228cf45908eeff92f9411`.
Runtime revision: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
Keep Unreal 5.8.3 CL 58210709, native CharacterMovement, Enhanced Input, current
UMG/Slate and the existing authority/filtered-view boundary. No engine or
mandatory ability/UI framework migration.

The v0.5 targets are right-click navigation, explicit attack/targeting, Q/W/E/R
skills, F context, selected-unit inspection, anchored own commands, minimap,
24-slot carried inventory, four item references, vendor buying and real consensual
atomic peer exchange. Retain v0.4 gameplay, short normal profile and earned
peaceful Heart route. No mana, new abilities, equipment economy or full conquest.

Adopt the supplied sprint defaults with audited baseline dispositions: retain
250 ms stale-hold expiry; convert the actual one starting ward charge exactly;
retain loss of unused charges on death with no respawn grant. Books and Seal
still drop. See B50 for source anchors and the synchronized JSON contract.

## Fresh readiness checks

Preflight passed. All 99 source/configuration hashes and seven payload hashes
match the retained B47 archive. The packaged regional smoke passed 93 checks.
Exact commands and local artifacts are indexed in B50. These establish the
unchanged v0.4 baseline, not v0.5 acceptance or human play. No new build/cook,
full unrelated test suite or long soak was run for this documentation update.

Archive: `Artifacts/20261021-092725-233-PackageWin64/Archive/Windows`.
Existing launch/scripts still use the retained V04 profile and controls until
implementation changes them; root README records that runtime behavior.

## First implementation work

Prove B51's native navigation adapter on two independent owning clients before
building the full HUD. Add order/life identity, supersession, stop epochs,
snapshot approach, path rejection/stall behavior, focus/liveness cancellation and
camera controls. Preserve sprint, Step, geometry, collision and correction rules.
Cursor-facing updates and existing generated/native input contexts are explicit
migration points; do not let them compete with travel facing or the new bindings.

Then follow B52 input → B53 HUD/privacy → B54 inventory → B55 vendor → B56 peer
trade → B57 presentation → B58 verification. Keep templates blank and record real
work in the working ledger and release reports. Run Unreal builds/probes
sequentially with source stable; BuildEditor/BuildGame support MaxParallelActions 4
on this 16 GB machine. Preserve authored assets, frozen references, the cook-safe
navigation envelope and separate web baseline.

## Deferred evidence

The ordinary B47 25-minute run passed. The separate interrupted stress run remains
deferred by user direction and is not a 90-minute pass. Do not automatically rerun
it absent a new lifetime concern. A fresh B47 cold checkout, remote Git/LFS delivery
and dedicated packaging remain unrun/conditional. B58 still requires a current
cold build/cook, affected delayed clients, ten restarts and a normal session.

Actual human multiplayer/usability, contrasting builds, counterplay and full
60–90 minute pacing remain unvalidated. No human observations were invented.
Preserve these gaps explicitly; archived v0.4 records remain historical evidence.
