# B62 — official material supply and regional commerce

23 October 2026. **B62 engineering implementation and focused verification are complete.** This milestone extends B61's actual item/cash ledger. Production,
institution schedules/shortage and the public Charter remain B63–B65 work.

## Implemented behavior

The City Guild's Official materials service sells one Fiber for 4 or Ore for 6
from the versioned runtime catalog. Each purchase validates final bag slots and
weight, allocates the actor first, burns carried gold and creates the output in
one causal receipt. Allocation/validation failure consumes nothing. The retained
Guild bookshop still sells its four books at the adopted fixed 40 price; there
is no official buyback, new ward grant or official Crystal/Toolkit/Core/Seal.

City, WestTown and EastTown use their existing physical B61 stock and finite
institutional reserves. City is co-located with the Guild/Treasury; West and
East use their saved regional station positions, looked up from the actual map
rather than hard-coded world coordinates. Static official landmarks identify
these services without labelling ordinary bodies or disabling ordinary PvP.

One-unit Buy transfers an existing market instance into the bag and wallet
payment into the reserve. Sell takes the exact selected, owned, current,
unreserved Fiber/Ore/Crystal/Toolkit instance into institutional stock and
transfers reserve cash into the wallet. Core/books/wards/Seal have no regional
buyback. There is no XP, reward mint, reverse recipe, stock/cash refill or
producer/demand behavior in these transactions.

Integer ask/bid rules retain the fixed clamp and next-stock guard. Every market
stock/cash change invalidates the entire institution quote, including cash-only
changes. The server validates the displayed revision and exact price and never
silently accepts changed terms. Selection from market stock is deterministic:
oldest creation receipt then item ID. Transfers retain identity and origin.

Authority sessions bind controller/match/life/locality; opening requires alive,
unprotected, actual body sight, clear segment, 120 cm and no conflicting action.
Bank/bookshop/peer services cancel mutually. Range/sight/life/action/combat change
closes the session, and closing the bag or losing focus cancels it. No remote
price updates occur after access closes. Queued commits run after damage/death
and before objective resolution and reject after the match deadline/result.
Requests use token limits, monotonically increasing IDs, bounded pending queues,
pending duplicate suppression and final receipt caching. Evicted receipt IDs
cannot replay. Shared item reservations remain the retained inventory authority.

The native bag provides four locality/supply open buttons, timestamped offers,
stock/caps, bid/ask, available reserve and explicit Buy/Sell-selected controls.
It shows affordability, weight/slot, stock-full/empty and reserve rejection
states. Pending disables exchanges. An unsolicited/stale quote needs explicit
Review before a separate new purchase/sale click. Offers contain only approved
institution facts; private owner state remains on owner RPCs.

Ledger validation precedes mutation; all fallible output allocation precedes
payment. The physical bag/holder projection is completed in the same authority
stack before publication. Every successful transaction is independently audited
against all live items and actual balances. Official receipts classify burns;
regional receipts contain cancelling per-account transfers, never mint/burn.

## Verification status

The first compile passed at
`Artifacts/20261023-143359-723-BuildEditor` (27 actions/162.24s), and the commerce
fixture compile passed at `Artifacts/20261023-143916-474-BuildEditor`
(15 actions/78.09s). All 22 native rules passed at
`Artifacts/20261023-144100-500-B62-Commerce/Rules`.

The first authority fixture exposed a pre-existing startup ordering fault:
a second local controller could enter BeginPlay before its local player was
attached, causing widget creation to fail and a null dereference. Local UI now
initializes only when a local player exists, with idempotent retry on tick.
The failed log is retained in that artifact folder. The corrected build passed
at `Artifacts/20261023-144354-300-BuildEditor` (28 actions/144.21s). The change
also guards widget allocation and preserves service switching across synchronous
local trade-cancel responses; no new framework or movement/input scheme was added.

`Scripts/RunCommerceCheck.ps1 -TimeoutSeconds 120` then **passed** at
`Artifacts/20261023-144655-549-B62-Commerce`: all **22 native rules** and **26
native authority checks**, zero failures. Two separately admitted local owner
records exercise queued quote competition and foreign-session rejection on one
authority. The checks also cover allocation failure, fixed-price payment/source,
replay, deterministic transfer/lineage, same-market loss, reservations, excluded
Core buyback, full stock, empty reserve/stock, cash-only revision invalidation,
full bag, excess weight, range cancellation and West/East body access.

Independent Python replay **passed** the actual exported journal: **450 endowed
+ 4000 labelled diagnostic mint - 268 official-purchase burns = 4182 gold**,
seven accounts, 143 distinct item identities, 140 receipts, zero audit failures.
Stock/cash fixture setup uses conserved transfers, and item cleanup is explicitly
labelled diagnostic consumption. None is a normal player faucet, restock policy
or gameplay progression claim. All source/config was frozen during each Unreal
operation. `git diff --check` passed.

Portable results and the exact source manifest are in [evidence/B62](evidence/B62/Authority.json).
Replay its ledger with `python Scripts/CheckEconomyEvidence.py docs/v06/evidence/B62/Ledger.json`.
The runner launches Unreal operations sequentially, with bounded timeouts, and
disables editor-only tooling plugins like the existing native runners.


## Limits

This is native source and standalone authority evidence, not independent remote
clients, OS-scale/input/layout review, a cooked package, human decisions or a
normal economy journey. Those release gates remain B66/B67. The existing v0.5
progression is retained until B65 replaces the legacy credit/Seal route. B64 will
add scheduled stock/cash producers/consumers and the public shortage event; no
unannounced price modifier is enabled ahead of that work.
