# Continuation — v0.5 B56 peer exchange checkpoint

22 October 2026. The user requested review/commit of master revision 5 and the
v0.5 specification, then requested implementation. B50 audit is complete;
[B51 navigation](v05/B51_IMPLEMENTATION.md) and
[B52 input/action modes](v05/B52_IMPLEMENTATION.md) and
[B53 HUD/inspection/minimap](v05/B53_IMPLEMENTATION.md) and
[B54 inventory/quick items](v05/B54_IMPLEMENTATION.md) and
[B55 quoted Guild vendor](v05/B55_IMPLEMENTATION.md) and
[B56 peer exchange](v05/B56_IMPLEMENTATION.md) are implemented. Continue
with B57 shared presentation, then B58 verification. Full v0.5 is not delivered.

**22 October update:** The user explicitly approved adopting installed Unreal
5.8.4 CL 58902052. The platform lock now names that build; its guard remains active.
B47–B52 evidence retains its original 5.8.3 provenance. Read the B53 report for
the actual engine transition, failures corrected, current evidence and limits.

## Read first

1. [Master plan](master/README.md) and [v0.5 spec](v05/README.md), all chapters,
   JSON and wireframes.
2. [B50 actual baseline/defaults](v05/B50_BASELINE_REVIEW.md),
   [working ledger](v05/FeatureLedger.csv), B51–B56 implementation reports.
3. [Build plan](v05/08_BUILD_PLAN.md), [acceptance](v05/09_ACCEPTANCE_AND_USER_REVIEW.md)
   and [build handoff](v05/10_CODEX_BUILD_PROMPT.md).
4. [B47 archive](v04/CONTINUATION_B47.md) and [B47 evidence](v04/B47_IMPLEMENTATION.md).

## Baseline and invariants

B47 runtime: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
B50 preparation: `c222021`. B51: `607e2d8`. B52: `9f4c58f`.
B53: `b0af13e`. B54: `a6476b4`. B55: `ccddfae`. B56 is the commit containing its implementation report.
Keep the approved Unreal 5.8.4 CL 58902052 baseline, native CharacterMovement, Enhanced Input, UMG/Slate
and the existing authority/filtered-view boundary. No engine/framework migration.

Retain actual class starts, six attributes, level-five cap, four shared skill
slots, 1500-second normal match and earned peaceful Heart route. No mana, new
abilities, equipment economy or full conquest. Preserve 100 ms hold refresh and
250 ms stale expiry. B54 converts exactly one starting ward to a finite instance once;
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

B53 replaces the old V05 HUD with anchored own commands/status, a masked
selected-subject inspector and a three-state filtered minimap. LMB/drag pans;
RMB first cancels arming, then issues only an authored-surface ground order.
Map sight never increases body reach. No observed/public fields were added;
ward Presentation distinguishes own from observed foreign sources. Controller
updates refresh the HUD without depending on painting. Duplicate nonzero request/
result pairs are suppressed in a bounded local cache. Death allows read-only
inspection; return clears targets and restores follow. Terminal UI follows the
actual objective receipt, including No acquisition.

Review with `Scripts/OpenStandalone.ps1`, Host private lobby and use F1. I/Bag opens the own carried inventory. Digits 1–4 resolve owned references:
hold a book shortcut to learn, or arm a ward and confirm ground. Bag Use on a book
arms a fresh F hold. Drop requires the named modal. T remains unavailable. Native default and 125% text captures are engineering evidence.
Final shared scaling/focus and the full Windows display-scale matrix belong to B57.

B53 uses approved UE 5.8.4: both targets and the cook passed; 17 warning-free rules,
movement 7, variable-frame combat 48 and V04 region 93 passed. Cooked HUD/input
passes Server 26 / A 37 / B 37 at measured 99.55/99.64 and 153.40/153.59 ms;
the report records every actual RTT rather than assuming the target name.
Cooked navigation passes 34/17/17; filtered-view privacy passes 12/29/27. Native
720p/1080p and 125% text captures were inspected. Archive:
`Artifacts/20261022-090127-479-PackageWin64/Archive/Windows`, with 121 source/config
and seven payload hashes matching. All are engineering fixtures, not human acceptance.

B54 provides a common server-only ALDItem record (books, finite ward charges and
Seal), owner-private FLDInventory, 24 slots and four identity references. Reorder
and binding change container revision without invalidating item availability or
learning. Pickup uses a version quote for a currently authorized observation,
then body sight/range/capacity checks again at commit. Bag Use arms a fresh F read;
a book shortcut holds its own key/button. Drop uses a named modal and legal ground
within 80 cm. Starting ward migration runs once; unused charges die, books/Seal
drop, quick references clear and return supplies remain empty. The old direct
V05 read/ward/pickup bypasses reject. V04 retained paths remain available explicitly.

The observed field mask remains eight and the public allowlist remains 22. The
bounded envelope is now 128 entities to accommodate four full 24-item death drops;
the 96-dropped-world-item fixture fits within 8 KiB without granting return supplies.
Read B54's report for the actual warm package, measured client echoes, byte sizes,
native captures and fixture limits. Full traffic/UI performance remains B58.
The historical B54 archive is
`Artifacts/20261022-104649-962-PackageWin64/Archive/Windows`; the B55 archive
below is now current. The B54 archive contains 131 source/configuration hashes and
seven payload hashes. `Artifacts/B54PackageProvenance.json` records the final audit. B54 has
18 warning-free rules, 48 combat and 93 V04 regional checks. Cooked inventory passes
59 / 25 / 25 at measured 115.62 / 99.30 and 148.99 / 149.10 ms; the final inventory
bases are 29 and 53 respectively, with 25 ms jitter. These are actual medians,
not an assertion that both lower-lane clients measured 100 ms. Cooked navigation
passes 34 / 20 / 20, HUD/input 26 / 37 / 37 and privacy 12 / 29 / 27. The report
records their different actual RTTs. Final 720p/1080p, 125% text, active learning,
named drop and dead-bag captures were visually inspected. B55 adds the quote/earned vendor proof below; full B57/B58 human, OS-scale,
lifetime and performance gates remain.

B55 adds the Guild's four-book unlimited catalog above the own bag, with live
catalog/session versions, an explicit one-item Buy and authority-owned receipts.
Known books are labelled and can still be bought for trade. The old unquoted V05
purchase path rejects; V04 retains its historical route. Stale prices require
review and a separate Buy, never an automatic retry. Pending requests create no
optimistic gold or item state. Authority creates/adopts one B54 physical book
before a single debit; failed or replayed requests conserve inventory and gold.

Leaving range/sight, positive damage, protection/action, life or match changes
invalidate the server session before queued purchases. Dismissed panels cannot
reopen from a delayed quote. Bag Use closes the shop and arms the existing fresh
read gesture. Client receipt and eight-field observed/22-field public boundaries
remain unchanged. The receipt discloses only public offers and the owner's state.

The B55 client earns two ordinary courier contracts from zero gold through native
navigation and actual F holds, then buys/reads one book for forty of its earned
fifty gold. No grants, teleports, protection edits or timer changes occur in that
segment; ordinary first-visit/courier awards remain active. Later 80-gold wallets,
full bags, price changes, failure injection and combat/death setup are explicitly
labelled engineering fixtures. No human task completion is claimed.

The historical B55 warm archive is
`Artifacts/20261022-152659-513-PackageWin64/Archive/Windows`, superseding the
historical archives above at that checkpoint. All 137 Source/Config and seven payload hashes match;
`Artifacts/B55PackageProvenance.json` binds the final audit to the B55 commit.
Both Editor/Game targets and the cook passed. Native vendor checks pass
23 / 19 / 14 at actual echo 170.55 / 169.87 ms. Both cooked vendor lanes pass
23 / 19 / 14 at 108.24 / 108.23 ms (target 100, base 29) and
149.59 / 149.56 ms (target 150, base 53), each with configured 25 ms jitter.
B55 also passes 18 warning-free rules, 48 variable-frame combat and 93 retained
V04 regional checks. Cooked inventory passes 59 / 25 / 25 at actual
153.32 / 152.14 ms; HUD/input passes 26 / 37 / 37 at 153.89 / 152.94 ms;
restricted-view privacy passes 12 / 29 / 27. Native and cooked 720p/1080p catalog,
full-bag and stale-quote captures, including 125% text, were visually inspected.
Read the B55 report for the artifact paths and actual engineering/human limits.

B56 implements observed neutral invitations, six-item per-side offers, physical
reservations, exposed gold, revisioned Ready and separate final Confirm. Both
capacity and one-Seal limits use the projected final bag, not pre-exchange fullness.
Transfer keeps physical IDs and clears only outgoing quick references. Cancel
unlocks retained references; gold changes reset both approvals without escrow or
silently adjusting offered amounts. No fee, XP, Witness or fabrication reset occurs.

The shared native bag contains invitations, both offer directions, gold entry,
Add/Remove, Ready, Review, Confirm and completed receipt. Partner handles are
session-scoped; no partner wallet, remaining bag, build, role or canonical actor
is supplied. The digest hashes only session/revision and offered terms, while
private life and physical availability versions are checked separately. The
19-rule runner now requires the named TradePrivacy allowlist test; regenerating
warm UBT discovery was necessary to include that newly added source file.

The current B56 warm archive is
`Artifacts/20261022-164716-551-PackageWin64/Archive/Windows`, superseding the
historical archives above. All 145 Source/Config and seven payload hashes match;
`Artifacts/B56PackageProvenance.json` binds the final audit to the B56 commit.
Both native targets, asset validation, cook and 19 warning-free rules passed.
Native trade cases and the actual earned B55 path remain distinct: B56 explicitly
seeds wallets/bags/craft receipts, uses observed native client consent and a real
one-second Pickpocket channel with a stationary NPC and forced roll, then isolates
adversarial states/time ages. These are engineering fixtures, not human evidence.
Final cooked trade passes 43 / 12 / 12 for due death, 42 / 12 / 12 for the
actual deadline, and 41 / 12 / 11 for a real remote disconnect. The measured
client echoes are 132.87 / 149.15, 98.98 / 99.26 and 149.26 / 149.21 ms respectively,
with configured jitter 25 ms. These measurements are not inferred from target
names. Nine final native package images (720p, 1080p and 125% text) were inspected.
Retained cooked vendor passes 23 / 19 / 14 at actual 149.20 / 149.13 ms;
inventory passes 59 / 25 / 25 at 149.36 / 149.10 ms; HUD/input passes
26 / 37 / 37 at 132.81 / 132.65 ms. Privacy passes 12 / 29 / 27 and navigation
34 / 20 / 20. The native 48 variable-frame combat and 93 V04 regional checks also
passed. Read the B56 report for packet settings, fixture boundaries and actual
engineering limits.

## Next work

B57 unifies presentation, focus and accessibility over the implemented commands.
Read the declared 720p/1080p at Windows 100/150% and 1440p at 100% matrix, with
normal and 125% text. Native offscreen images are engineering evidence, never OS
scaling or human acceptance. Reflow the lower HUD, keep own commands/status and
critical warnings visible, and refine the existing shared bag/vendor/trade shell.
Both trade directions and summary must remain visible; six-row trays and the own
bag can scroll, while Confirm/Cancel must stay reachable. Add the full Review
confirmation sublayer and deliberate Tab/Shift-Tab/Enter/Space focus traversal. Replace implementation
jargon in ordinary player copy without weakening version checks.
Preserve the distinction between Ready and final Confirm and require fresh physical
input after a panel closes. Numeric fields must consume digits without world actions.

B56's TradeDirector owns the session truth. Widgets cache the displayed trade
revision/digest for consent, and partner item handles are session-scoped random
IDs. Only own offered handles resolve own inventory. Never add partner balances,
remaining inventory, actor pointers, role flags or permanent IDs to TradeView.
Its 20-field allowlist and the three offered-item fields have a named rule test.
Preserve physical reservations with locked quick references, exposed gold, wallet
revision invalidation, projected final capacity/one-Seal checks, and both atomic
transfers before publication. Death/disconnect releases reservations before loss
and drops. The trade sample follows retained noncombat/objective processing;
terminal state prevents queued confirmation. Vendor Invalidate is the authoritative
service boundary; PC.VendorQuote remains only a client read model.

B58 then completes cold build, restart, normal UI journey, performance and actual
human review. Preserve actual fixture limits and retained unrelated valid evidence.

Keep templates blank and record actual results in working reports/ledger. Run
Unreal builds and probes sequentially with source stable; use MaxParallelActions 4
on this 16 GB machine. Preserve authored assets, frozen references/input assets,
engine pin and separate web baseline. `RunInputCheck.ps1`, `RunHUDCheck.ps1`,
`RunNavigationCheck.ps1`, `RunInventoryCheck.ps1`, `RunVendorCheck.ps1`, `RunTradeCheck.ps1` and
`CaptureInputUI.ps1` accept PackageRoot;
`PackageWin64.ps1` records source/payload hashes. RunHUDCheck expects Server 26 /
A 37 / B 37, including restricted canonical-actor and duplicate-receipt checks.
RunNavigationCheck now expects 34 / 20 / 20, including unique observed candidates
with ambient residents held at authored homes for approach fixtures. The B54 report
retains the initial failed navigation run and unchanged successful rerun.
The final near-150 ms HUD lane uses OneWayBaseMs 49 with JitterMs 25; the ~100 ms
lane uses 30. Always record measured transport rather than assuming the target.

## Deferred evidence

B47's ordinary 25-minute session passed. The user stopped the separate stress run;
it is not a 90-minute pass and must not automatically resume absent a new lifetime
concern. Current cold checkout, affected ten restarts, normal v0.5 25-minute journey
and zero-kill earned UI progression remain for B58. Remote Git/LFS delivery and
source-engine dedicated packaging remain unverified/conditional. Cooked headless
listen authority is labelled. Actual human multiplayer/usability, contrasting builds,
counterplay and full 60–90 minute pacing remain unvalidated. No observations were invented.
