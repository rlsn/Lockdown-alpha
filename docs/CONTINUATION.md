# Continuation — v0.5 B52 input checkpoint

21 October 2026. The user requested review/commit of master revision 5 and the
v0.5 specification, then requested implementation. B50 audit is complete;
[B51 navigation](v05/B51_IMPLEMENTATION.md) and
[B52 input/action modes](v05/B52_IMPLEMENTATION.md) are implemented. Continue
with B53 HUD/selection/minimap, then B54–B58. Full v0.5 is not delivered.

## Read first

1. [Master plan](master/README.md) and [v0.5 spec](v05/README.md), all chapters,
   JSON and wireframes.
2. [B50 actual baseline/defaults](v05/B50_BASELINE_REVIEW.md),
   [working ledger](v05/FeatureLedger.csv), B51 and B52 implementation reports.
3. [Build plan](v05/08_BUILD_PLAN.md), [acceptance](v05/09_ACCEPTANCE_AND_USER_REVIEW.md)
   and [build handoff](v05/10_CODEX_BUILD_PROMPT.md).
4. [B47 archive](v04/CONTINUATION_B47.md) and [B47 evidence](v04/B47_IMPLEMENTATION.md).

## Baseline and invariants

B47 runtime: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
B50 preparation: `c222021`. B51: `607e2d8`. B52 is the commit containing its report.
Keep Unreal 5.8.3 CL 58210709, native CharacterMovement, Enhanced Input, UMG/Slate
and the existing authority/filtered-view boundary. No engine/framework migration.

Retain actual class starts, six attributes, level-five cap, four shared skill
slots, 1500-second normal match and earned peaceful Heart route. No mana, new
abilities, equipment economy or full conquest. Preserve 100 ms hold refresh and
250 ms stale expiry. Convert exactly one starting ward to finite instances in B54;
unused charges are lost on death, with no respawn grant. Books and Seal still drop.
Learned state, Witness and credits remain outside inventory. Preserve the eight-field
observed mask and 22-field public allowlist; new receipts contain only own state/facing.

## Implemented checkpoint

B51 uses validated owner steering through native CharacterMovement. Server and
owner compute static paths independently. Saved moves carry match/life/order/epoch;
old acknowledgements cannot revive stopped movement. Snapshot approach, bounded
replan, liveness, ramps, sprint, Step, falling and new life have independent cooked
proof. Preserve the sandbox cook-safe bounds fallback and saved regional envelopes.

B52 installs a 26-command registry and removes legacy V05 contexts. Normal Host
and implicit regional launch now use V05; explicit `Profile=V04` preserves historical
regressions. Q/W/E/R arm equipped skills; A then world confirmation attacks; F is
context; S stops; Space recenters. F1 offers saved rebinding, conflict rejection,
reserved Escape and local preferences. Labels read live mappings. Exact skills
resolve authorized observations; old unslotted/hover-facing RPC paths reject.
Panels stop/release and require physical release before fresh input. Apply/Use
close and arm a new context hold; the temporary character panel arms fabrication.

B52 Editor evidence: 16 warning-free rules, variable-frame combat 48, region 93,
and independent input Server 15 / A 19 / B 19 at measured echo 149.59/149.62 ms
with configured 25 ms jitter. The B52 report records final package/cooked/capture
proof and fixture boundaries. Cooked input passes 15/19/19 at measured ~100 and
~150 ms; the navigation regression passes 34/17/17. Final archive:
`Artifacts/20261021-163038-208-PackageWin64/Archive/Windows` (113 source/config
and seven payload hashes match). These are engineering clients, not human acceptance.

Review with `Scripts/OpenStandalone.ps1`, Host the controls checkpoint and use F1.
The old HUD is temporary. Numeric items report empty; I opens the retained books/
development panel; T is unavailable. Saved text scale currently affects controls
rows; final shared scaling/focus belongs to B57.

## Next work

B53: anchored own commands/status, selected-subject inspector, reticle/range/action
states, three-state fog and minimap pan/drag/authored-surface right-click navigation.
Use only own state plus filtered views. Other private fields remain Unknown/absent;
map gestures never cast, select hidden units or increase body reach. Unify frontend,
lobby, death and results. B54 adds the real 24-slot container and four identity
references; B55 quoted vendor; B56 six-item per-side atomic consensual exchange
with exposed gold, revisioned Ready and separate Confirm; B57 presentation; B58 verification.

Keep templates blank and record actual results in working reports/ledger. Run
Unreal builds and probes sequentially with source stable; use MaxParallelActions 4
on this 16 GB machine. Preserve authored assets, frozen references/input assets,
engine pin and separate web baseline. `RunInputCheck.ps1`, `RunNavigationCheck.ps1`
and `CaptureInputUI.ps1` accept PackageRoot; `PackageWin64.ps1` records source/payload hashes.

## Deferred evidence

B47's ordinary 25-minute session passed. The user stopped the separate stress run;
it is not a 90-minute pass and must not automatically resume absent a new lifetime
concern. Current cold checkout, affected ten restarts, normal v0.5 25-minute journey
and zero-kill earned UI progression remain for B58. Remote Git/LFS delivery and
source-engine dedicated packaging remain unverified/conditional. Cooked headless
listen authority is labelled. Actual human multiplayer/usability, contrasting builds,
counterplay and full 60–90 minute pacing remain unvalidated. No observations were invented.