# Continuation — v0.6 B61 cargo foundation; accounting next

23 October 2026. The user directed: “avoid lengthy tests, keep verifications
minimal” and said the project is moving onto the next version. Follow that
constraint. The v0.5 engineering handoff is recorded; full release acceptance
remains partial. The user supplied master revision 6 and the complete v0.6
economy specifications on 23 October. B60 source/document audit is complete.
B61 goods/cargo foundation is implemented and compiles; B61 remains partial.

## Read first

1. [B60 baseline review](v06/B60_BASELINE_REVIEW.md), [v0.6 scope](v06/README.md),
   [build plan](v06/07_BUILD_PLAN.md), [banking/cargo](v06/11_BANKING_AND_CARGO.md),
   [working ledger](v06/FeatureLedger.csv) and `v06/data/economy_rules.json`.
2. [B58 handoff](v05/B58_HANDOFF.md): delivered behavior, retained evidence and open gates.
3. [B57 report](v05/B57_IMPLEMENTATION.md) and [archived continuation](v05/CONTINUATION_B57.md): detailed implementation and evidence.
4. [Working ledger](v05/FeatureLedger.csv), [master plan](master/README.md) and [v0.5 specification](v05/README.md).
5. Complete v0.6 documents override earlier private-Seal/no-weight/no-bank clauses.

## Current implementation direction

Read [B61 cargo foundation](v06/B61_CARGO_FOUNDATION.md). Five goods append to
physical kinds; native final slot/weight checks now cover adoption/pickup,
vendor, both trade sides and preparation start/commit. Cargo drops on life end,
uses generic filtered observation/pickup and cannot quick-use. Bag has weight and
Cargo filter. Item origin/parent fields are server-only storage, not populated
accounting. Native constants currently match proposed weights; runtime catalog
integration remains open. No material supply or normal economy is enabled.

Editor build passed: `Artifacts/20261023-102029-314-BuildEditor` (23 actions,
304.62s, compile cap4). Bounded rule launch
`Artifacts/20261023-131741-354-Automation` hit 60s during editor-plugin startup,
before tests: no new rule passes, including CargoCapacity, are claimed. Do not
repeat/extend testing automatically. Source/Config was frozen during both runs.

Next finish B61 persistent bank/institutions/nodes, budget identities, versioned
runtime catalog and receipts/reconciliation. Then B62 supply/markets, B63 production/skill uses,
B64 scheduled routines/shortage, B65 distinct endorsements and registered
public-window Charter, B66 native service UI, B67 scoped acceptance. The normal
frontend retains the v0.5 loop plus the cargo foundation until the new economy is integrated.
No new normal physical Seal, courier-credit fabrication or private bypass in
the final v0.6 profile. Everyone may claim during its 30-warning/60-open window.
Record unimplemented work honestly; B60 is an audit, not gameplay acceptance.

Initial NPC gold is lazy: eight resident purses at 36 plus a possible Sentinel
purse at 36. The spec's 738 including markets is illustrative; retain and count
the existing Sentinel policy (774 with all nine purses), never silently omit it.
Logout removes the contender and disallows late rejoin: retain admission policy
and account explicitly for retired wallets; do not invent reconnect scope.

## Retained pre-economy baseline

Pre-economy runtime commit: `bd86a85e440e5699bbe91627d696296cd6518e9b` (B57).
B58 changed only handoff/index/ledger documentation. This old package does not
contain B61 cargo changes. The retained warm Development
package remains `Artifacts/20261022-220153-009-PackageWin64/Archive/Windows`.
A brief package audit on 23 October confirmed all 145 Source/Config files and
seven runtime payload hashes. Existing JSON reports remain green: 19 rule tests;
trade death 43/17/12, disconnect 41/17/11, terminal 42/17/12; vendor 23/19/14;
inventory 59/25/25; HUD/input 26/37/37. These tests were not rerun for the handoff.

B51–B57 deliver navigation, mappings/targeting, private inspection/own HUD/minimap,
physical inventory/quick references, quoted vendor, consensual peer exchange and
shared panels/focus/final review. Keep the approved Unreal 5.8.4 CL 58902052 pin.
Do not replace native CharacterMovement, Enhanced Input or UMG/Slate.

## Carry forward

Current cold checkout/build/cook, ten restarts, normal 25-minute v0.5 journey,
performance comparison, the on-screen OS-scale matrix and actual human review
remain unverified. Retained v0.4 lifetime/peaceful evidence and v0.5 engineering
fixtures do not satisfy those missing current-version gates. Detailed evidence
and presentation deviations are in the B58 handoff. Do not mark all U01–U56 passed.
Do not automatically launch long runs or resume the user-deferred 90-minute stress.

Preserve class starts, six attributes, level-five cap, four shared skill slots,
1500-second normal match and earned peaceful Heart route. Hold refresh is 100 ms,
stale expiry 250 ms. Finite wards migrate once and unused charges are lost on
death without a return grant. Books and legacy Seal drop; knowledge/Witness/credits stay
outside the bag in v0.5. v0.6 replaces credits/Seal with distinct endorsements and
registered Charter; carried goods drop and bank/progress persist. Preserve eight-field observed and 22-field public allowlists,
and the 20-field TradeView / three-field offered-item allowlists.

TradeDirector owns session truth. Widgets cache displayed revision/digest;
partner handles are temporary and disclose no remaining bag/wallet/role. Preserve
reservations, exposed gold, wallet invalidation, net capacity/one-Seal checks,
separate Ready/Confirm and atomic transfer. Release reservations before death
loss/drops; terminal state wins over queued confirmation. VendorQuote is a client
read model; authoritative invalidation belongs to Vendor.

Use short checks appropriate to the next change. If an Unreal build/probe is
needed, run it sequentially with other Unreal work, cap compile actions at four,
and keep Source/Config frozen until it finishes. Frozen design files are not
silently rewritten to turn deferred acceptance into passes.
