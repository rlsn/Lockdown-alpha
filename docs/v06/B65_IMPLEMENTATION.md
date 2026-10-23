# B65 — procurement and the public Charter contest

23 October 2026. Implements B65 on B64 `f21e7ce`, with user authorization to
continue and commit. The normal frontend now uses the registered Charter route.
B66 native presentation integration and B67 release acceptance remain open.

Core procurement at City consumes one exact reserved Core for 80 gold/60 XP.
The first completion records a retained Industrial endorsement; repeat payments
stop at three per contender and 3*N globally. West Supply consumes two distinct
Toolkits for 50 gold/30 XP, once per contender and N globally, recording its
separate Supply endorsement. Both use a stationary three-second hold, six seconds
without positive dealt/taken damage, alive/unprotected body sight and 120 cm.
They reuse production reservation/release, current match/life/body/revisions and
fresh hold checks. Cancellation releases original instances; exhausted quotas
reject without consuming surplus. Bought, crafted, peer-owned or recovered
inputs use the same ownership check; possession alone grants no endorsement.

The ledger now accepts additional finite-budget debits in the same transaction.
Global and per-contender quotas, exact input consumption and reward mint validate
before any mutation. XP and retained owner receipts publish after that successful
boundary. The independent checker also verifies account/item expected revisions,
distinct budget debits, exact consumed parents and final causal item records.
Existing B63/B64 portable ledgers remain compatible and passed the stricter replay.

Treasury checkout requires Supply, Industrial, retained Witness and exactly 100
gold. The owner reviews an explicit wallet share 0–100 and bank remainder, then
confirms separately. Current physical service/session/match/life, quiet/range/sight,
balances and both account revisions validate before a single 100-gold burn.
A successful purchase creates a bound owner registration, never an inventory item.
Only one purchase and successful activation are possible per contender. The UI
explains that delaying activation can forfeit the remaining deadline opportunity.

At the City civic terminal (the retained Guild anchor), the production panel or
character-panel command arms a fresh four-second context hold. Purchased unused
registration, recovery, quiet/life/range/sight, current body and no overlapping
warning/opening are checked throughout. Purchase requires 94 seconds remaining;
activation must leave its full subsequent 30-second warning and 60-second opening
before match deadline. Successful activation spends the registration and writes
public window identity/endpoints; another activation cannot queue or extend it.

Before activation, death retains endorsements and registration. Successful actual
respawn completion, including delayed completion, sets readiness to Now+30.
Another death recomputes this from its newest successful respawn; duplicate death
cannot extend the return timer or refill order quotas. After activation, the spent
flag and window live independently of the purchaser's pawn/connection. Death
was exercised; actual remote disconnect remains part of B67 acceptance.

Every living unprotected contender may use the retained ten-second Heart claim
while open, without any Charter. Body sight/range120, contest240, movement,
positive damage, stale hold and current life/body checks remain authoritative.
The public attempt sensor is retained. Scheduled claim completion must fit the
captured window close and match deadline. An exact-close completion may commit
on a late sample with a fresh valid hold; the result records its scheduled
endpoint. A late start or interrupted attempt receives no extra claim time.
Public endpoint sampling precedes Heart eligibility; closure protects ordinary
access. Towers remain intact. Result publication cancels production reservations,
trade and service sessions before the final accounting export.

Normal couriers still pay 25 gold/45 XP for eight jobs and now award zero legacy
credits. Normal legacy Seal creation/adoption/pickup, four-credit fabrication,
fixture seeding and defence-flag bypass are rejected, including direct requests.
Labelled non-economy legacy profiles retain their old routes. New private owner
fields are individually reflected; the observed tuple remains exactly eight
fields. The public GameState allowlist now has 34 fields, adding only public
quotas and window identity/phase/endpoints, with no purchaser identity or position.

Native bag controls preview both exact orders and activation; Treasury has a
separate Charter review/confirm path. HUD shows retained endorsements, Witness,
registration and public warning/opening countdowns. B66 still owns full screen,
keyboard/remapping/focus/layout and restricted-client presentation acceptance.

Verification on the final source:

- Editor build: `Artifacts/20261023-185449-131-BuildEditor`, four actions,
  20.75 seconds; pinned UE5.8.4 CL58902052, compile worker cap4.
- `Scripts/RunCharterCheck.ps1`: `Artifacts/20261023-185529-879-B65-Charter`;
  23 native rules, 56 authority checks and 39 earned-route checks; zero failures.
- Authority lane uses labelled diagnostic resources. It covers cancellation,
  quiet/damage/stale/movement, recovered input identities, both exhausted global
  quotas, replay/foreign/stale/negative checkout, actual native Treasury review,
  repeated deaths, deadline limits, overlapping activation rejection, contest,
  closure/tie/late samples and terminal result freeze.
- Authority replay: 38 receipts, seven accounts, 97 unique item identities;
  450 + 1000 diagnostic mint + 580 procurement mint - 450 burns = 1580 gold.
- Earned route disables diagnostic item grants and starts at zero money/XP with
  the actual Laborer build. Equipped Heave earns Witness; three real courier
  completions earn75. Four paid-input Toolkit crafts provide Supply and Core
  ingredients; City Crystal costs12; actual procurement pays50+80. Checkout
  leaves15 carried gold. Three actual visit-gate boundary crossings produce300
  XP/level4. A different contender without a Charter wins the public exact-close
  claim after activation. No gold/item/XP/endorsement setter funds this lane.
- Earned replay: 28 receipts, seven accounts, 103 unique identities;
  450 + 205 reward mints - 178 burns = 477 aggregate gold. Wallet15 plus market
  reserves462 reconcile; Crystal12 is a transfer into City, not a burn.
- All final ledgers have zero authority audit failures and pass independent Python
  replay. Portable normalized reports, ledgers, provenance and final physical
  source SHA-256 hashes are under [evidence/B65](evidence/B65/Summary.json).

One preliminary journey check used exact floating-point subtraction equality for
30/60-second durations. Activation and the later claim succeeded; the final
probe uses a 1e-6 tolerance and exports measured endpoints. The final complete
pass covers that correction. No runtime change followed the final successful
build/checks. Source/Config stayed frozen during every Unreal operation.

These are controlled local authority checks: fixture placement/clock and legal
walking-boundary samples replace travel/input for brevity. The earned resources
and commits are real; this is not a human or remote native-input playthrough.
B64 retained actual road navigation evidence; B66/B67 retain rendered screens,
remote clients, current cook/package, normal timed journeys, restarts, performance
and human acceptance. No long/stress run was added and no push was requested.
