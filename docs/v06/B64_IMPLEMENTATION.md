# B64 — institutions, shortage and geography

23 October 2026. Implements B64 on B63 commit `46998d0`. The user authorized
continuing and commits. Procurement/endorsements/registered Charter remain B65;
full input/layout/network/package and human acceptance remain B66/B67.

## Scheduled institutional work

The staged catalog now validates the eight tick endpoints 180,360,...,1440,
exact producer/consumer rows and shortage contract. At each due opportunity,
West may create one Fiber for a two-gold reserve burn and East one Ore for
three gold. Output allocation, reserve/budget validation and the ledger commit
precede physical publication. Each producer has a match-global eight-unit
budget; payments leave circulation and never credit another hidden purse.

Both producers execute before consumers. West consumes one actual Fiber, East
one Ore and City one Toolkit, selecting oldest origin receipt then GUID. Each
creation/payment and consumption is a distinct causal receipt, including when
net raw stock is unchanged. Empty, full, insufficient cash and allocation
failure are explicit skips with no item, spend or deferred obligation. Due
opportunities advance once by scheduled index; receipt identities bind schedule,
tick and operation. Repeated samples and recreating service markers cannot
reset this state. Late sampling processes due opportunities only while the
match is playing; none catch up after the deadline or terminal result.

Institution sampling runs after damage/death release, before basic production,
bank and player commerce. Stock commits and reserve revisions invalidate quotes
even when creation plus consumption leaves net stock unchanged. No prices are
silently charged against an obsolete player quote.

Five public protected service actors represent the routines. They have no
combat character, personal purse, loot or navigation blocker. Only these
explicit official actors get the public Institution kind; ordinary bodies keep
their masked presentation. The existing eight-field observed tuple carries
routine address, bounded public status and work endpoint. A three-second local
arm-work animation follows the committed receipt and cannot trigger accounting.
Public next-work countdown and status distinguish working, waiting and skipped
states. No hidden contender inventory participates in institutional decisions.

## Shortage and shared order

East Toolkit alone uses the catalog's 5/4 ratio from elapsed 600 inclusive to
780 exclusive, before unchanged base-price clamps. Ask and bid share that ratio
and the next-stock guard. The market revision gains a phase epoch at both
boundaries, including a sample that crosses the whole window. A public HUD cue
announces active, filled and expired order state. Prices remain elevated until
780 even if the order fills early. No stock/cash refill or second event occurs.

The native workshop panel can select exactly one owned Toolkit and preview a
three-second shortage delivery for 40 gold/20 XP with no endorsement. It closes
and arms a fresh context hold using the shared production reservation/channel
path. Alive/unprotected, current owner/match/life/body, stationary hold, body
sight/range, exact input revision and shared budget are checked. Neither an
accepted channel nor its reservation claims the global order. At completion,
one transaction consumes only the winner's Toolkit, mints 40 and spends the
single global quota; XP follows that success. Race losers and interruptions
keep inputs and receive no payout. Completion must be strictly before 780;
start rejects if its scheduled completion cannot satisfy that endpoint. This
order adds no old commission credit and does not consume an Analyze coupon.

## Regional placement and measured travel

Retained saved-map public and covered roads remain unchanged. Supply/markets
and workshops use their City/West/East service anchors; two outcrops remain at
WildCrossing road endpoints. Routine positions use saved public settlement and
resident-start metadata, keeping at least 160 cm from authored ordinary
residents and separating producer/consumer stations. No private live positions
are queried. Both saved maps passed actual ground, complete nav-path and body
sight/range access checks; covered-route geometry blocks geometric sight.

The fixture drives actual click navigation and CharacterMovement through each
public/covered route's waypoints. Endpoints are placed between routes; movement
within each measured route is native. World time is accelerated 4x for a brief
run, and the table records authority seconds. Measured walk speed was 178.2 cm/s
in this native fixture; these are not sprint/build comparisons or human choices.

| Map | Destination | Route | Travel seconds | Public-watch exposure seconds |
| --- | --- | --- | ---: | ---: |
| Civic Reach | WestTown | Public | 21.19 | 0.00 |
| Civic Reach | WestTown | Covered | 31.17 | 0.00 |
| Civic Reach | EastTown | Public | 22.73 | 6.50 |
| Civic Reach | EastTown | Covered | 46.79 | 0.00 |
| Reflected Reach | WestTown | Public | 24.26 | 0.00 |
| Reflected Reach | WestTown | Covered | 35.91 | 0.00 |
| Reflected Reach | EastTown | Public | 26.17 | 6.42 |
| Reflected Reach | EastTown | Covered | 53.88 | 0.00 |

East's measured covered routes traded longer travel for zero watch exposure;
West's two tested routes were both outside the public watch. That result does
not measure another contender's sight, pursuit, cargo loss or strategic benefit.

## Verification and provenance

Final Editor build passed at `Artifacts/20261023-154440-754-BuildEditor`
(four actions, 8.59s). Core verification at
`Artifacts/20261023-153728-223-B64-Institutions` passed **23 rules and 36 authority
checks**, including all eight funded opportunities, explicit skips, allocation
failure, marker recreation, endpoint quote rejection, competing shortage
channels, premium persistence, expiry and terminal closure. Independent replay
passed **83 receipts, seven accounts, 124 item identities, zero audit failures**:
**450 endowed + 2000 labelled diagnostic mint + 40 shortage mint - 40 producer
burns = 2450 gold**. Final West/East reserves are 134/126; funded raw production
and consumption leave their initial 18 units intact. City consumed its two
actual Toolkits and skipped its six later opportunities.

After that core run, routine placements were moved away from authored resident
starts and geography clearance was strengthened. The focused final rerun at
`Artifacts/20261023-154207-432-B64-Institutions` passed **17 geography checks per
map and eight actual native routes**. The subsequent market countdown copy/
calculation change was compiled; transaction code and reflected schemas were
unchanged. No unrelated rule/transaction suites were repeated for these changes.

Run `Scripts/RunInstitutionCheck.ps1 -TimeoutSeconds 120` for the complete lane;
`-AuthorityOnly` or `-GeographyOnly` selects focused verification. Unreal lanes
are sequential. Source/Config were frozen during every Unreal operation and
compiler actions capped at four. `git diff --check` passes. Portable results,
normalized JSON, final source hashes and exact per-lane provenance are in
[evidence/B64](evidence/B64/Summary.json). Replay with
`python Scripts/CheckEconomyEvidence.py docs/v06/evidence/B64/Ledger.json`.

The economic fixture uses two local owners on one authority, controlled clock,
labelled diagnostic stock/item/money setup, cleanup and conserved cash transfers.
The shortage payout and producer fees use actual production transactions, not
substitute diagnostic rewards. Native travel uses the engine's real movement;
physical keyboard/mouse input, rendered layout/animation quality, remote latency,
current cook/package, human route choice and normal zero-grant progression are
unrun B66/B67 gates. No long session, stress or cook was launched. The retained
legacy credit/physical Seal route remains pending B65 replacement.
