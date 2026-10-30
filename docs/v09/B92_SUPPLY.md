# B92 — physical official supply and shared books

30 October 2026 · B92 remains **in progress**

Native source **4835f7b05e88b9096184e98bad6cca8cb5fa0de5**. [Frozen evidence](evidence/B92Supply/README.md)
extends the [bank and commodity services](B92_BANK.md).

## Scope and endpoint roles

Five authored supply counters now offer inherited catalog goods with a positive
official price, plus Fiber at4 and Ore at6 gold. Prices come from the selected
v0.8 equipment catalog; unsupported or unpriced goods are not fabricated. Supply
counters also offer Circle-I books at40. Highfort Barracks offers Circle-II at60;
Scholars Archive offers Circle-II at60 and Circle-III at90. Circle-IV books remain
outside official purchase and keep their assigned unique-source boundary.

This follows the endpoint roles in the selected service data. It does not add
Circle-III sales to every supply shop. The two-copy Circle-III limit is nevertheless
one match-wide budget per definition, independent of endpoint, controller, owner
or life. No official stock is refilled by reopening a panel or transferring
control. Additional authorized endpoints must use that same budget. Visiting
multiple distinct Circle-III sale sites remains unverified because only the
selected archive currently has that role.

LMB selects an official counter and uses the existing approach/access path.
The paginated private panel lists current definitions, prices and remaining
limited stock. One Buy confirmation creates one actual item in the owner's bag.
No new interaction key or physical button hold is introduced. Official purchases
burn gold; regional commodity sales/buys transfer cash and stock. There is no
official gear/book buyback or passive XP.

## Authority and identity

The server rechecks physical eligibility, endpoint role, definition/skill, price,
current supply quote, wallet and inventory revisions, slots/weight and funds.
For Circle-III it also spends one global quota unit. Allocation precedes the
atomic ledger commit; failure spends nothing. The same receipt records wallet
burn, creation and quota debit. The physical actor, exact bag entry and canonical
ledger retain that identity and provenance. No parallel official-item ledger
or unknown-creation exception is introduced.

Exact request replay returns the original result; changed terms cannot reuse
the confirmation. Purchasing from one owner changes the shared quote, so another
owner must confirm current terms. Sold-out and wrong-endpoint requests spend
nothing. Ordinary profiles and the standalone125-check economy domain remain
separate from this explicit `v09_population` service integration; `v09_full`
admission stays closed.

## Evidence and limits

The connected suite passes **52 checks**. Independent replay closes **181
receipts,149 accounts,573 historical item identities and3,103 gold**, with zero
mint and185 burn:180 for two Circle-III books,4 for Fiber and1 deposit fee.
The prior21 commodity transactions, source harvest/consume, capacity refusal,
remote takeover and cross-city bank withdrawal remain in the same scenario.
The **59 rule tests**, **125 native economy checks**, **37 four-controller roster
checks** and both full-unity targets pass. All **207 protected inputs** remain
unchanged. The initial archive-routing failure is retained in a diagnostic
folder; the corrected run is separate.

The connected scenario retains bank, market, source, capacity and takeover
coverage, then two real remote owners buy both copies of one Circle-III definition.
The first purchase is duplicated; it still creates one book and burns90 once.
A third purchase rejects. A supply counter creates one Fiber for4 and refuses
a forged Circle-III order outside its role. The final audit reconciles actual
bags, canonical origins and depleted quota. A 720p capture records the supply UI. The initial paginated list still needs category/search, text scaling and human usability review.

Funding this isolated purchase test explicitly transfers400 existing Capital
reserve gold:300 to the replacement owner and100 to the other owner. It mints
nothing, but it is not normal earnings, a complete acquisition journey or an
affordability/balance claim. Travel, later source tools and capacity preparation
retain their labelled fixture boundaries. Learning/binding purchased books and
exhaustive purchasing of every catalog row remain future integration coverage.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather
python -B Scripts/ExportB92SupplyEvidence.py docs/v09/evidence/B92Supply --verify
```

Next: finite deposits, physical contracts/crafting, full-map prepared Witness
and an earned zero-kill Charter journey; then routines/security/hostiles and
ordinary contender AI/tier transitions. Full recovery, authenticated reconnect,
B93 victory and B94 normal-session/human/performance acceptance remain open.
