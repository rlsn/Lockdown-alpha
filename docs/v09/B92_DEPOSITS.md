# B92 — finite automatic deposit extraction

30 October 2026 · B92 remains **in progress**

Native source **d09a76bbe551eed20855bf6f11fd30bf187c1f3d**. [Frozen evidence](evidence/B92Deposits/README.md)
extends the [official supply checkpoint](B92_SUPPLY.md).

## Basic extraction and control policy

The selected nine resource POIs now have one six-charge budget each: five Crystal,
two Ore and two Fiber deposits. Each charge creates one real item. There is no
gold or XP reward, periodic refill, respawn reset or allowance per controller.
The total deposit endowment is54 extractable units:30 Crystal,12 Ore and12 Fiber.
Budgets belong to the match ledger, not a visual marker or current player body.

Basic extraction is an **automatic eligible POI**, not a new interaction verb or
key. Stand within120 cm with body sight, alive, unprotected, stationary on the
ground, six seconds combat quiet, free hands and capacity for the output. The
server starts a six-second channel and the existing progress UI displays it.
No button is held. A completed channel creates one item and starts another full
six-second channel only while eligibility and charges remain. Late samples never
compress missed time into multiple outputs.

S, focus/order cancellation, movement, damage, lost range/sight, competing action,
life/control change or invalid capacity ends the pending channel without spending
a charge. After stopping/interruption, leave the deposit and return to restart;
standing still must not immediately override the player's Stop command. Ordinary
Mute/Silence alone do not prohibit the basic automatic POI operation. This does
not bypass the categories of actual learned or item actions.

The selected **Survey** shortcut remains open. Its one-second Crystal operation
must later use an actually learned/prepared skill, threshold12, normal costs and
cooldown, and this same charge/capacity boundary. This increment adds no free
generic mining/harvesting ability or new forestry-item function.

## One commit and one identity

The deposit authority validates the current body/life, stationary anchor, quiet,
action and physical eligibility through completion. Allocation and capacity
validation precede the canonical transaction. One receipt spends one source
charge and creates one output at the persistent seat wallet location. The real
bag/actor retain that identity and origin. Failed allocation or completion spends
nothing. New extraction outputs join the existing world/market/official ledger;
moving or dropping them transfers those identities rather than recreating them.

Channel sequencing is compared within its channel domain, so deposit progress
cannot be discarded just because an earlier door/item request had a larger
sequence number. B91 source/control regressions remain required.

## Validation and limits

The connected suite passes **59 checks**. Independent replay closes **287
receipts,149 accounts,627 historical item identities and3,103 gold**. Extraction
creates54 distinct units and spends exactly six charges at every node, with no
mint, gold transfer/burn or XP of its own. The previous bank/official-purchase
burn remains185. The 720p capture shows the automatic Crystal progress bar.
Rules59, economy125, four-controller roster37, two-remote B91 source/control53
and both full-unity targets pass. All207 protected inputs remain unchanged.
The initial compiler diagnostic is retained separately from passing evidence.

The connected scenario observes a real automatic progress bar, issues Stop once
and waits beyond the channel duration to verify no restart or charge spend.
It then completes actual six-second Ore, Crystal and Fiber channels. A full
24-slot bag prevents channel start and preserves all six charges at another node.
The owner's XP remains unchanged.

An explicit authority fixture drains the remaining charges through the canonical
creation/budget commit and drops each real output on the ground to keep capacity.
That checks all-nine source closure,54 distinct outputs, correct goods and exhausted
refusal; it is not54 normal-duration channels, a natural travel/acquisition run or
a human pacing test. Counter travel, paid-supply funding and source preparation
retain their prior labelled fixture boundaries. Broad interruption/status/race
and Survey integration coverage remain future acceptance work.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/RunB92Economy.ps1
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/BuildGame.ps1 -FullUnity -Gather
python -B Scripts/ExportB92DepositsEvidence.py docs/v09/evidence/B92Deposits --verify
```

Next: the prepared Survey shortcut, physical contracts/crafting, full-map Witness
and earned zero-kill Charter progression; routines/security/hostiles, legal AI,
tier transitions, authenticated reconnect and full active-match recovery remain
open. B92 remains in progress and `v09_full` admission stays closed. B93 victory
and B94 normal-session/human/performance acceptance remain separate.
