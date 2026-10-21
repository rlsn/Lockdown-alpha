# Continuation — v0.5 B51 navigation checkpoint

21 October 2026. The user supplied master plan revision 5 and the v0.5 UI/control
specification and requested review, commit and preparation for implementation.
The user subsequently requested implementation. [B51 native navigation](v05/B51_IMPLEMENTATION.md)
is implemented and verified in independent Editor and cooked clients. Continue
with B52 input/action modes before expanding the full HUD.

## Read first

1. [Master plan revision 5](master/README.md), especially UI/controls and privacy.
2. [v0.5 specification](v05/README.md), all chapters, JSON and wireframes.
3. [Actual baseline and adopted defaults](v05/B50_BASELINE_REVIEW.md) and
   [working feature ledger](v05/FeatureLedger.csv), then the [B51 report](v05/B51_IMPLEMENTATION.md).
4. [B50–B58 build plan](v05/08_BUILD_PLAN.md),
   [U01–U56 acceptance](v05/09_ACCEPTANCE_AND_USER_REVIEW.md) and
   [build handoff](v05/10_CODEX_BUILD_PROMPT.md).
5. [Archived B47 continuation](v04/CONTINUATION_B47.md) and
   [B47 engineering evidence](v04/B47_IMPLEMENTATION.md).

## Baseline and scope

B50 predecessor handoff: `b8f003a6535079c98dd228cf45908eeff92f9411`.
B47 predecessor runtime: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
The B51 runtime checkpoint is the commit containing its implementation report.
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

## Current implementation and evidence

The selected adapter is validated owner steering through native CharacterMovement,
with independent server path validation. No route or hidden actor pointer is
streamed to the owner. Saved moves include match/life/order/epoch, and stale
movement cannot revive a stopped route. Snapshot approaches, one-replan stalls,
navigation liveness, native sprint/Step, focus, gravity and respawn are exercised.

Editor and cooked runs each pass Server 34 / A 17 / B 17. Measured cooked echo
medians cover about 100 and 150 ms; initial acknowledgement p95 remains below
250 ms. Retained rules (15), movement (7), variable-frame combat (48) and region
(93) pass. The observed mask remains eight fields; the exact public allowlist adds
only the profile boolean, making 22 fields. B51 records corrections, failed runs,
fixture boundaries and the cooked sandbox navigation-envelope repair.

Final B51 archive: `Artifacts/20261021-152127-712-PackageWin64/Archive/Windows`.
Its 103 source/configuration hashes and seven payload hashes match the checkpoint.
The normal frontend still launches V04 until B52; explicitly review the V05
navigation lobby with `Scripts/OpenStandalone.ps1 -NavigationCheckpoint`.

## Next implementation work

Implement B52's single mapping registry, rebinding/persistence, Q/W/E/R skills,
1–4 item entry points, F holds, A-then-click attack, explicit targeting, gesture
ownership and fresh input after panels/focus loss. Remove the old normal-profile
contexts coherently, retain V04 for historical regressions and preserve the B51
movement metadata. Integrate preparation Apply as close-and-arm followed by a
fresh station hold; exact targeted skills must resolve authorized observed handles.

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
