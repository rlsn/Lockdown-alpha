# B92 — finite economy authority foundation

29 October 2026 · B92 remains **in progress**

Native source: **b0d8034ec24ea3916d4c8350353615bb7126d2cb**. [Frozen evidence](evidence/B92Economy/README.md)
pins the exact code, data and scripts. This increment establishes the full-map
economic authority before admitting a populated match. It does not complete
B92-T01, T05, T07 or the complete stage.

## Delivered boundary

`FCatalog::LoadFull` loads the selected schema9 economy through a separate,
digest-checked entry point. Schema1's three pools, eight courier rewards,
eight institution opportunities and original timing remain intact. The new
staged file is a normalized copy of the supplied economy, not a second design
catalog. `SyncB92Economy.py --check` detects drift. Supplied normalized SHA256:
`fb876c2f3cd03ff3c3b826c8f949392400b2fe0c81860bb71d7df73ee8b7830a`.

`ALDB92Economy` is an authority-only, nonreplicated, nonticking actor. It requires
exactly 24 unique contender IDs and 96 disjoint civilian IDs supplied by a caller.
It initializes once, to a candidate ledger and preallocated physical stock;
invalid identity/configuration/allocation cannot publish a partial endowment.
Respawn, human/AI split and a later controller count cannot refill its budgets.

| Initial institutional custody | Gold | Fiber | Ore | Crystal | Toolkit |
| --- | ---: | ---: | ---: | ---: | ---: |
| Crownreach / capital | 600 | 54 | 18 | 9 | 6 |
| Highfort | 600 | 18 | 54 | 9 | 6 |
| Lumen Garden / scholars | 600 | 24 | 24 | 24 | 6 |
| Riverport | 600 | 54 | 18 | 9 | 6 |
| Ironworks | 600 | 18 | 54 | 9 | 6 |

These are **426 distinct physical `ALDItem` instances**, each with one creation
receipt and one address. The ledger has 149 accounting identities: five markets,
96 civilian purses at three gold, 24 contender wallets and 24 shared contender
banks at zero. Total initial gold is 3,288. These counts are not a population
spawn/roster demonstration. Officials, travelers and guards add no spendable
endowment here.

Each contender has 20 paid courier and 40 issuance budget units; Core/Supply
personal caps are three/one, shared caps 72/24. The single shortage order and
29 opportunities per producer have independent bounds. This increment reserves
the budgets; it implements no courier delivery or procurement payout shortcut.

Native Buy/Sell uses the inherited integer stock-price formula, current pool
and wallet revisions, exact item revisions and one ledger commit. A purchase
selects an existing stock instance on the server; the caller cannot name an
unseen stock identity. Sale rejects unavailable/reserved/bound/noncommodity
instances, full stock and insufficient reserves. It preserves the original
identity and creation lineage. A repeated identical confirmation returns its
original result; altered terms under the same receipt reject. Cash changes and
shortage boundaries invalidate quotes as well as stock changes.

Items remain in account-addressed **institutional custody**, including the
test purchaser's goods. These APIs are internal authority operations, not a
client commerce permit. Normal service range/sight/quiet/life, inventory capacity,
binding, observation and RPC checks must surround the eventual bag projection.
No new control keys or world-interaction ability were introduced.

## Funded institutions and time

An explicit monotonic authority clock samples the 29 opportunities at
180–5,220 seconds. Each opportunity attempts funded Riverport Fiber production
for two gold, then Ironworks Ore for three, followed by actual Fiber/Ore/Capital
Toolkit consumption. A successful row allocates/validates before its one receipt
and then projects the physical creation or destruction. Empty stock, insufficient
cash and full capacity skip independently. Skips cannot retry inside the same
interval. If a later sample has crossed a following 180-second interval, that
older opportunity is recorded as missed instead of executing a burst of catch-up
work. At 5,400 seconds the domain closes. A fatal internal row inconsistency also
closes it rather than allowing partial work to be retried.

Ironworks Toolkit quotes use the selected 5/4 shortage ratio during
`[1800,1980)`. Entering/leaving that interval has a distinct quote revision.
The shortage payout itself still needs the full-map contract/service adapter.

## Baseline audit and integration mismatches

| Question | Actual repository result | B92 consequence |
| --- | --- | --- |
| Contender starting money | `FLDOwnerState` defaults wallet/bank to zero; normal `PostLogin` grants neither | Keep the selected 3,288 start and 23,362 pre-burn maximum; diagnostic grants are not baseline income |
| Physical starter kit | Normal tactical admission sets Ward charges to zero; inventory initializes empty; `TacticalStart` supplies learned ranks/actions for all nine presets | Do not clone starter books, gear or consumables when creating AI seats |
| Resident purse | Legacy `LDWorldChecks::WalletId` lazily endows 36 gold per nonhuman wallet | Do not enable that lazy purse beside the selected 96 × 3-gold population; bind the persistent population identities instead |
| Markets | Legacy City/WestTown/EastTown, three × 150 gold and 84 items | Use the five full-profile pools once; existing regional aliases cannot create another pool |
| Institutions | Legacy eight scheduled opportunities, old shortage interval and endpoints | Use the full catalog and one authority clock; never run both schedules |
| Work caps | Legacy eight courier rewards and controller-count procurement totals | Use fixed 24-seat budgets and separate 40-issuance/20-delivery accounting |
| Witness | Existing `LDWorldChecks::UseCharter`, `LDCharter::Skill`, `LDDevelopment::SkillValue` and `LDSkillExecution::RequestEquipped` implement it | Reuse the real prepared skill operation; no free Witness grant or substitute generic interaction |

Witness requires an active, unprotected contender, an equipped learned
Heave/Analyze/Parley/Survey action, visible body-sight target within 120 cm and
score at least 12. The native score is effective STR/INT/CHA/WIS respectively,
plus two per rank above one; Heave/Parley/Survey have their existing subclass
bonuses. The score rejection precedes cooldown spend. Heave/Analyze/Parley
channel for two seconds, Survey for one; cooldowns are 6/6/8/6 seconds. A
successful commit grants one receipt and zero gold/XP, and a second receipt is
refused. Normal skill/control gates still apply.

The baseline skill executor retains its historical heartbeat/release machinery.
Full-map Witness service presentation must use the user's one-confirmation
channel policy with S cancellation. The operation exists; its **full-map binding
and zero-kill earned Charter journey are not demonstrated by this audit**.

## Native validation

| Run | Result and scope |
| --- | --- |
| Dedicated full-map economy probe | **125 checks**; three independent disposable authority domains |
| Five-pool transactions | Five initial quotes, purchases and resales; real stock identity; exact replay; altered/stale quote and item refusal; one-winner serialized quote race; unlisted book and civilian-identity rejection |
| Complete schedule domain | **139 receipts**, 149 accounts, 484 historical item identities; 58 creations, 64 consumptions, 420 live instances; 145 burn, zero mint, 3,143 final gold |
| Poor/full/empty domain | **11 receipts**, 428 historical identities, 424 live; skipped production cannot retry after funding/capacity returns; five burn, zero mint, 3,283 gold |
| Late-wake domain | **6 receipts**, 428 historical identities, 425 live; ten missed rows and one executed opportunity; invalid/backward time refusal; five burn, zero mint, 3,283 gold |
| Native rules | **58 passed**, including both B92 catalog/starter audits; 872 stock states checked at normal and shortage ratios |
| Inherited commerce | **26 checks**, 140 independently replayed receipts, seven accounts, 143 item identities, 4,182 final gold; inherited diagnostic reward remains explicitly separate |
| Inherited institutions | **36 checks**, 83 replayed receipts, seven accounts, 124 item identities, 2,450 final gold |
| Builds | Editor and Game Development, full unity, pinned UE5.8.4 CL58902052 |
| Protected inputs | All 148 protected assets and 59 supplied files byte-for-byte unchanged |

The schedule test explicitly advances event time; it is not a 90-minute session
or hardware measurement. Contender IDs and civilian IDs are fixture identities,
not 220 spawned population bodies. Purchase funding transfers 100 existing gold
from each pool into five test wallets. It is labelled `DiagnosticFixtureTransfer`,
never a reward mint, legal courier completion or ordinary player earnings.
The three fixture domains are independent and must not be summed as one match.
The account-custody race runs on one dedicated authority, not two remote clients.

The first Editor compile failed because the probe used an unavailable Unreal
`TNumericLimits::QuietNaN` helper. It was corrected to the standard C++ numeric
limits function before final builds/tests. The failed log remains a diagnostic
artifact, not passing acceptance.

Reproduce, strictly sequentially:

```powershell
python -B Scripts/SyncB92Economy.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 2 -FullUnity
./Scripts/RunB92Economy.ps1
./Scripts/RunTests.ps1
./Scripts/RunB92EconomyRegressions.ps1
./Scripts/BuildGame.ps1 -MaxParallelActions 2 -FullUnity
python -B Scripts/CheckB92EconomyEvidence.py docs/v09/evidence/B92Economy
```

## Remaining B92 work, in order

1. Bind the 24 persistent seats and 196 noncontender capacities to actual bodies
   and takeover/lifecycle behavior. Preserve the audited starter/endowment once.
2. Connect five physical service endpoints, LMB service UI and remote requests
   to this authority. Project purchases into real inventories and enforce all
   ordinary access/capacity gates. Merge B91 world/container/installed sources
   and normal owner inventories into **one full-match item ledger** before
   admitting `v09_full`; the review-domain custody is not a second live economy.
3. Connect global per-definition official stock, bank access, finite deposits,
   normal crafting, shared orders, physical courier tokens and earned Witness.
   Demonstrate zero-kill registered Charter with actual costs, receipts and travel.
4. Import the 152 routine assignments; implement role behavior, witnessed local
   security, same-ID guard recovery and zero-reward ambient bodies.
5. Run ordinary contender AI through owned/observed/public inputs and legal
   commands; wake tiers without teleporting or duplicating identities/receipts.
6. Add integrated account/economy recovery and complete all 13 B92 acceptance
   cases. B93 route execution and B94 full-session/human/performance gates remain
   separate.

`v09_full` playable admission remains closed. `v09_environment` and v0.8 retain
their existing behavior. The B92 probe is an explicit development flag inside
the ownerless B91 environment host; it does not enable the old economy timer,
legacy lazy purses, population, ordinary service shortcuts or victory executors.
