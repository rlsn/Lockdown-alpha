# B63 — production and equipped utility

23 October 2026. Implements the B63 production gate on B62 commit `76c0e2c`.
The user authorized committing and proceeding with B63. B64 schedules/geography,
B65 procurement/Charter and B66/B67 release acceptance remain separate gates.

## Implemented behavior

Two server-only outcrop anchors at WildCrossing share the B61 match ledger's
three-charge budgets. A stationary, living, unprotected body within 120 cm and
body sight can hold context for six seconds to create one Crystal. The final
capacity check and charge decrement share the output creation transaction. A
race loser, interrupted channel or allocation failure spends no charge. Neither
respawn nor another owner initializes these budgets again. No gathering gold
or XP is awarded.

City, West and East workshops support the same basic recipes for every class:
Toolkit consumes two Fiber and one Ore, burns four gold and takes four seconds;
Core consumes two Toolkits and one Crystal, burns six gold and takes six seconds.
The native bag panel selects exact instances, displays their short IDs, fee,
final slots/weight and coupon, then closes and arms a fresh context hold. It
spends nothing while previewing. Normal basic gathering is also available by
context at an outcrop; workshops open the bag for explicit selection.

Crafting requires six seconds since positive damage dealt/taken. Accepted
channels reserve the exact instances using the same physical and bag reservation
flags as peer exchange, retaining their ownership and weight. Gold remains in
the carried wallet. Current life/body, stationarity/anchor, body sight/range,
recipe revision, exact ownership/revisions/reservations, fees, wallet, coupon and
net capacity are revalidated before completion. A movement pulse, displacement,
damage, stale hold beyond 250 ms, release, death, source loss, range/sight loss
or changed input cancels and unlocks retained inputs. Death/disconnect releases
reservations before inventory drops. Damage resolution and deaths precede basic
production commits in the shared authority batch.

Output actors are allocated before spending. One ledger transaction consumes
the selected identities, burns the fee and creates the output with those exact
parents and an origin receipt. Only then is the physical bag/wallet projection
published. Consumed parents remain in the causal journal. Net capacity permits
a full bag to craft when removing the inputs makes room. No crafting XP,
reverse recipes or random yield is introduced.

Survey requires its learned/equipped slot and score 12. Both normal skill
selection and the production context shortcut call the existing `RequestEquipped`
path, with its normal one-second channel, hold arbitration and cooldown. It
uses the same extraction budget/capacity transaction. Analyze requires its
learned/equipped slot and score 14, normal two-second channel and cooldown. A
successful plan replaces the owner's single private workshop-bound coupon:
two gold off, thirty seconds, no stacking or trade. Only successful discounted
crafting consumes it. A coupon that cannot survive scheduled completion rejects
before reservation; expiry/binding is rechecked at commit, and death clears it.
Basic gathering/crafting requires no utility slot. Existing Witness alternatives
and combat formulas remain unchanged.

Production sites appear through the existing sanitized eight-field observation
tuple, with an appended Production kind. Public site address and remaining
charges are the only production facts in that tuple. World markers/labels,
inspection text, minimap markers and held progress support the native path.
Two explicit public GameState counters expose legitimate outcrop depletion;
coupon identity, site and expiry remain owner-private. Final route placement,
travel timing and visual/input/layout acceptance remain B64/B66/B67.

## Reflection correction

The first verification passed all 49 authority checks but failed the public
schema count: Unreal reflected only the first variable in a comma-separated
`UPROPERTY` declaration. Production plan/owner fields and public counters now
have individual declarations. The same issue discovered in B61 bank and B62
commerce payloads is corrected here. `EconomyPayload` checks every intended
reflected field and a serialized production-plan round trip; public privacy and
production-kind bounds are covered by the retained view tests. This is source
and serialization evidence, not an independent remote-client session.

## Verification

Final Editor build **passed** at `Artifacts/20261023-151455-801-BuildEditor`
(41 actions, 191.88 seconds). Final bounded verification **passed** at
`Artifacts/20261023-151835-702-B63-Production`: **23 rules, 49 authority checks,
zero failures**. Independent receipt replay **passed**: **450 endowed + 2000
labelled diagnostic mint - 165 burns = 2285 gold**, seven accounts, 156 item
identities, 136 receipts and zero audit failures. Of the burns, 16 are successful
recipe fees and 149 is the real death tax; failed/interrupted crafts spend zero.

Portable evidence is in [evidence/B63](evidence/B63/Authority.json), including
rules, authority checks, actual ledger, summary and SHA-256 source manifest.
Replay with `python Scripts/CheckEconomyEvidence.py docs/v06/evidence/B63/Ledger.json`.
The runner requires at least the 49 checks present in this run. Earlier evidence
at `Artifacts/20261023-151233-124-B63-Production` retains its failed public schema
rule and successful authority/replay results; the final corrected run supersedes
that gate. `git diff --check` passed.

The runner is `Scripts/RunProductionCheck.ps1 -TimeoutSeconds 120`. It runs
Unreal lanes sequentially and independently replays the actual exported ledger
with `Scripts/CheckEconomyEvidence.py`. Source/Config remain frozen during
Unreal operations and Editor compile actions are capped at four.

The fixture uses two local owners on one authority, controlled clock advances,
labelled diagnostic item/gold setup, a conserved late-wallet transfer, a labelled
Analyze attribute fixture and explicit cleanup. Checks cover extraction timing,
replay, interruption, final-charge competition, exact reservations/lineage,
allocation failure, full-bag net capacity, a real positive hit at the craft
deadline, late wallet loss, equipped utility/cooldown/score rules, coupon
replacement/cancellation/binding/expiry, native preview/arm methods and real
contender death/respawn. It does not establish human travel choices, physical
keyboard/mouse behavior, OS-scale layout, remote latency or a cooked package.
No long session, stress or cook/package was launched.
