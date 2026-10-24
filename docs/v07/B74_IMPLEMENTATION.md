# B74 — acquisition and assembly

24 October 2026. Based on B73 `ee2e13b`. The v07 integration now provides the
acquisition transaction layer. Complete v0.7 effects, the authored guardian/world
and four-route integration, and release acceptance remain B75–B77.

## Sources and availability

City Official Supply sells all28 priced item definitions at fixed unlimited
prices, with no buyback. It burns wallet gold and creates one physical instance.
City books remain40g, CircleII books cost60g at West/East, and CircleIII books
cost90g at East Archive. The latter use two paid creations per spell globally,
decremented only in the successful purchase receipt. Reopening or recreating
the service cannot refill a ledger budget. Legacy books remain distinct.

Acquisition metadata covers all40 item definitions and all12 recipes. It does
not activate unfinished effects. The twelve completed passive gear definitions
include B73's eleven plus Broker's Seal; Null Bell also works. Other equipment
and consumables can be acquired/traded/used as exact recipe cargo, with their
future B75 effects/bindings clearly labelled. Unsupported spells can have a
physical book source, but reading/equipping/casting still refuses them until
their effects are implemented. Nothing is silently aliased to a legacy skill.

`acquisition.json`, a generated checksum contract and native
`DA_AcquisitionCatalog` carry the checked metadata. Runtime does not read docs.
The equipment asset is refreshed for Broker's Seal. The combat asset and its14
implemented spell effects stay unchanged. Bootstrap keeps the28 original
packages and validates all three tactical catalogs.

## Exact recipes, work orders and quality

The existing production director owns every new stationary hold. Town recipes
take4s; advanced recipes take6s at East; Heartbreaker takes8s at HeartForge.
All12 exact recipes are public and project final count/weight after removing
inputs. The same authority path validates profile, catalog digest, recipe,
station, match/life, build/inventory revisions, exact physical IDs/revisions,
reservation state, fee and funds. Inputs are selected in visible bag-slot order.
Equipped or reserved inputs cannot be selected; explicitly unbind first.

Assembly requires six quiet seconds, no released own projectile, body sight and
120cm range, then the held stationary commit. Positive damage, movement, hard
control, release/focus loss, stale hold, changed revisions, death or terminal
state cancels without consumption. Silence/mute alone do not prevent this
institutional transaction. Carried gold is not escrowed or protected.

East work orders buy only missing immediate officially priced components and
burn their prices plus the authored fee. Toolkit/Core/Crystal/catalysts/Fragment
must already be supplied; no recursive fabrication or T4 work order exists.
Purchased subcomponents are transient create-and-consume lineage records within
the same atomic receipt; they never occupy extra bag slots. The output actor is
allocated before the ledger commit. Every consumed parent is recorded once.
Outputs are Standard and preserve the maximum input instance cooldown endpoint.

Standard→Fine consumes one Toolkit and6g over4s at a workshop. Fine→Masterwork
consumes one Toolkit plus one Crystal and12g over6s at East. Quality preserves
the target ID, tier, causal parents and cooldown endpoint. Only numeric weapon
damage, flat armor and eligible future item barriers use1.07/1.14, floored.
Attributes, capacity, movement, potency and active timing are unchanged. Targets
with no eligible numeric field refuse upgrade. Assembly consumes component
craftsmanship and discloses its loss in the preview. No XP or gold reward,
randomness, disassembly or reverse conversion is added.

Bound Broker's Seal reduces only the equipment assembly/quality fee by one,
minimum one. It does not discount purchased components, books or legacy fees.
Analyze coupons remain restricted to the original Toolkit/Core recipes.

## Finite caches and future world connection

The retained native region has physical service adapters for Ruin A(-1400,800),
B(0,1050), C(1450,800) and HeartForge(East service offset -200,+220). These are
explicit interim locations, rather than a claim that B76's world has shipped.
Each cache owns one unique key, one catalyst and its named capstone book from
the single reset endowment. Closed contents are institution-held and counted
once. A quiet4s open exposes the shared contents only to the requesting owner;
opening transfers nothing. Each pickup validates the actual cache ownership and
final capacity. Keys take3s quiet hold; other contents use explicit pickup.
Concurrent pickups cannot produce per-player copies. Ordinary spare gear,
materials, keys and objective cargo drop on death; only bound supported ordinary
equipment retains. Dropped metadata/material cargo is visible through the same
sanitized generic cargo observation and can be picked up normally.

The guardian drop adapter requires an actual `GuardianDefeat` receipt and one
unspent global budget before creating a contestable Fragment and Sovereign Ward
book. The actual guardian and paid defeat receipt are B76 work; the retained
respawning Sentinel is not substituted for it. Consequently Heartbreaker's
recipe can be projected and validated now, but its normal complete source/route
journey still depends on B76. The three ruin capstone copies exist now; their
effects remain B75. These boundaries remain partial in the working ledger.

## Wire, privacy and UI

Supply uses a versioned fixed32-byte digest envelope. The production plan has
bounded four-instance input serialization, a fixed digest and three additional
owned request fields: catalog digest, mode and target. Public observations retain
eight fields and GameState retains34. The public production presentation accepts
the nine actual service IDs. Cache contents travel only as a requested owner
service response after opening; they are not added to opponent observations.
Voluntarily offered trade items now disclose their quality; completed receipts
retain that value after ownership changes. Hidden inventory,
wallet, causal IDs and private builds remain excluded. Quality survives transfer.

Inventory adds Supply, Gear craft and Ruins sections in v07. Supplies show prices
and effect availability; recipes show exact owned inputs, missing purchases,
total fee, Standard output, final capacity and component-quality loss. A separate
confirm arms the existing fresh-context hold. Quality and item endpoints are
shown on owned items. Legacy V05 tabs and economic behavior remain gated.

## Verification and provenance

Final native results and hashes live in `evidence/B74/Summary.json`. The earned
local two-owner flow uses actual courier income, quoted component purchases,
Toolkit/Core production, quality upgrades, a Masterwork trade and return trade,
a staff branch, CircleII read/mastery,
cache opening/pickup and a catalyst-backed completed work order. It grants no
diagnostic money/items/XP. It controls placement, quiet/protection setup and the
authority clock; it is not a human travel or full-session acceptance run.

The separate server/two-independent-client boundary lane labels its diagnostic
wallet/Toolkit setup and tests real bounded RPCs, duplicate purchase replay,
concurrent CircleIII stock, cancellation, same-ID quality, equipped-input refusal
and the shared-key race. Rule tests cover all recipe projections, quality limits,
transient lineage, same-ID quality replay and bounded input serialization.
B71–B73 and V05 regressions are recorded separately. The final localized receipt
display correction and earned round-trip test followed the core network/asset runs;
final Editor build, earned journey and rules revalidate those changes.

The historical B56 `RunTradeCheck.ps1` attempt is archived as incompatible, not
passed: its V05 fixture asserts that a legacy Seal can be adopted, while the
current economy deliberately disables that path. No bypass is enabled to satisfy
that obsolete fixture. The earned B74 journey performs actual consent-based
Masterwork exchange and return exchange, including both completed receipts.

The adopted93-file specification is not edited by this milestone. One existing
whitespace change in `09_VICTORY_AND_MATCH_STATE.md` was present before work began;
it is preserved in the working tree and excluded from the B74 commit. The other92
files still match the adoption manifest exactly. `V07_TRANSITION.md` stays deleted.

B75 still owns27 remaining item effects/bindings and14 new spell effects. B76
owns the actual guardian/world and access routes. B77 and inherited V06-D01–D08
retain rendered/physical input, full network/privacy, packaging, normal sessions,
restart/performance and human acceptance. No push is requested.
