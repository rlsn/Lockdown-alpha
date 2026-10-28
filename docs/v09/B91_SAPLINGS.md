# B91 — finite sapling harvesting and clearance

28 October 2026 · source c483d4398f4148c2aaa57cb891bc92f9c776c3c2 · T10 remains partial

This checkpoint admits all 64 authored saplings with 80 HP, a four-second basic
harvest/chop interaction and ordinary melee clearance. Each sapling has its
original unique one-time source ID. Harvest awards at most one physical Fiber;
chopping and damage award none. It does not implement burning, ranged/projectile
or spell damage against saplings, grass concealment or saved-match restoration.
Those dependencies remain open rather than being represented by cosmetic verbs.

There are now **384/480 functional sources present**, including the saplings at
this partial behavior boundary. The 16 additional traversal anchors bring the
initial object registry to 328 records. Initial physical items remain 132;
successful harvests can add at most 64 finite Fiber instances during the match.
The native scenario harvests three sources and conserves all 135 resulting item
identities. Ninety-six grass sources and 3,000 scenery-tree targets remain unfitted.

## Transactions and combat

Harvest and chop are ordinary world actions: current observed handle/revision,
120 cm reach, body sight, six quiet seconds, four held seconds and the existing
life, protection, control, movement, damage and heartbeat checks. Mute/Silence
alone permit them. Root does not grant movement; it does not prohibit a stationary
basic world interaction. Stun/Hex block it through the existing service gate.
Only harvesting requires bag capacity and the current inventory revision.

At harvest commit, authority adopts one Fiber into the bag, records its finite
source and originating interaction receipt, then clears the sapling. A failed
capacity/adoption check leaves the source unspent. Source and yield identities
remain on the cleared object; retries cannot recreate the output. New Fiber
instances enter the same conservation registry as the original world items.
There is no XP, gold, equipment or extra action/binding reference reward.

Chop clears the sapling without minting anything. Ordinary melee reaches it
through the existing attack RPC, windup, shared staged hit list and cooldown, when no normal
combat target is selected in that strike. The sapling must currently be visible,
in the committed facing cone and actual melee reach on the same floor. It takes
the committed raw basic damage against its 80 HP; partial hits change its
revision, and lethal damage removes it without Fiber. Target selection leaves the pre-damage world unchanged for the whole impact batch. Melee impact refreshes
combat-quiet time and exposure at impact. Ranged weapons do not get an instant
clearance shortcut through this helper.

Saplings block walking and simplified sight while alive. Harvest, chop and lethal
melee share the same cleared state and collision/sight/navigation revision.
Observed cleared proxies lose collision; hidden changes remain private until
observed. Live HP, yield identity and receipt are absent from the seven-field
object snapshot. Blocking capability no longer implies storage: saplings cannot
be opened, stored into, carried, pushed or broken through a container verb.

## Native evidence and limits

[The source audit](evidence/B91Saplings/Audit.json) retains reports, received
client records and raw logs. Earlier checkpoint evidence is unchanged.

| Check | Result |
| --- | --- |
| Object compiler | Nine boundary/reproducibility tests, including unique sapling sources and no destruction reward |
| Native sapling fixture | 28 checks; two independent remote headless owners |
| All 64 sources | Supported fitted pads, 80 HP, distinct source IDs, actual blocking sight rays and no container capability |
| Harvest race/replay | One winner, one Fiber and one stable source receipt |
| Chop and ordinary melee | Real four-second chop; repeated ordinary attack RPCs clear 80 HP; neither yields Fiber |
| Cancellation/control | Released harvest preserves source; Mute/Silence allow basic harvesting |
| Capacity | Bag is filled with existing ground items; refused harvest spends nothing; restored capacity permits a later source |
| Life and warm initialization | Actual end-life/return plus same-world Initialize preserve exhaustion and cleared proxies |
| Conservation | Three unique harvest yields; one chop and one melee clearance; 135 physical identities have one location each |
| Regressions | Furniture 51; storage/cargo 49; links 55; fuel 32; structure 75 / 52 walks; independent doors; rules 51 |
| Builds | Editor and Game Development, full unity |

Capacity setup moves existing ground items into and back out of the bag; it
creates no fixture goods. The life test uses the actual return delay. Warm
Initialize is an idempotence test inside the same world, **not** a cross-process
saved-match reload. Different saplings exercise harvest, chop and melee paths;
this does not close the composite same-source burn/reload acceptance sentence.
Participants are staged within interaction reach; this fixture does not validate
the complete click-to-approach and context-selection experience around blockers.
The environment profile still runs no full-map economy or population, so this
does not establish B92 source accounting or AI progression.

Burning/socket field reservations, ranged/spell sapling damage, saved-match
restoration, grass/concealment, paid full-map fields/contact, scenery fitting and
density/human acceptance remain open. Evidence is editor authority with headless
remote clients; it establishes no rendered foliage or packaged-server acceptance.

```powershell
python -B Scripts/SyncB91Objects.py --check
python -B Scripts/SyncB91Tools.py --check
python -B Scripts/SyncB91Traversal.py --check
python -B Scripts/TestB91ObjectsImport.py
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Saplings -Port 7929
```

Prototype controls: hold **F** near a sapling to harvest, hold **Z** to chop
without reward, and press **E** for an ordinary melee attack in its direction.
Release, movement or Escape cancels a held interaction. Existing shared action,
accessory and combat permission rules remain in force.
