# B91 portable tools and retained fuel

Verified 28 October 2026 at native source
**0476cb4544247be56bfc7efc7be7f799f4974668**. The tool overlay checksum is
`b4bf9949720d5b6ccd592fd0b4af76439a5a59f882f8255d14d97f353e472b7a`;
the combined world request contract is
`696602945084bf92ffe07cb8182564072a92327d09900852838d88752f157eac`.
See the [audit](evidence/B91Tools/Audit.json) and [evidence guide](evidence/B91Tools/README.md).
This extends the [storage/cargo checkpoint](B91_OBJECTS.md)
with 24 Torches, 12 Lanterns and 12 Ropes. B91 remains in progress.

## Physical ownership and catalog boundary

The three definitions use append-only item values 94–96 and a separate,
checksum-closed overlay. The unchanged v0.8 equipment catalog still contains
80 items. The overlay adds Standard-only accessories of weight 2/3/3, with no
passive stats, official price or material output. Ordinary acquisition does not
mint them. New tool adoption is restricted to the environment profile.

There are now 204 fitted storage/cargo/tool sources plus 72 doors: 276 of the
480 functional source instances. The world starts with 132 finite physical
items, including the existing cache contents. The new source instances use the
same deterministic bounded fitting and physical inventory authority.

Fuel belongs to the physical item. Torch starts with 180,000 ms; Lantern starts
with 300,000 ms; Rope has no fuel. A lit item derives remaining fuel from its
stored remainder and authoritative clock origin. Transfers, binding changes,
drops and life return preserve that clock. Extinguishing retains the remainder;
expiry cannot refill or destroy the item. Passive clock progression does not
invalidate a structural inventory draft every frame; explicit toggles and expiry
do increment the item revision. Quiet draft publication refreshes display fuel
from the same physical instance before publishing.

Torch/Lantern activation requires the current item, bag revision, accessory
binding, life, match and compiled world contract. It uses the item control gate,
including Mute. A known ground identity cannot activate a dropped emitter.
Retrying an activation sequence returns the original receipt. No mana, extra
shared action or quick-reference slot is introduced. Tools drop on end-life even
when bound. Peer transfers preserve the same item's retained fields.

Rope can be picked up, stored, traded and bound. Attachment and traversal remain
disabled until the approved-anchor/link implementation is verified. Torch
socket ignition is likewise separate from cosmetic light activation.

## Light and observation

Torch emits a cosmetic 6 m light; Lantern emits 8 m. Neither adds sight,
detection, damage, a combat field or automatic ground fire. Body light cues are
sent only with an already authorized body presentation. Ground light is shown
only through the observed object proxy. Inventory fuel values stay in owner
state; the public body/object records contain no fuel or physical item identity.

The full-map sight path now has an explicit 18 m base for body, object and target
authorization, with inherited Blind at 2 m and the existing capped equipment/
trait bonuses. This fixes the previous environment path's use of the legacy
7.2 m helper. The legacy profile and its helper remain unchanged. Cosmetic light
does not alter either rule.

## Development controls

I cycles the selected bag item. Hold 1/2/3/4 to bind it to accessory 1/2/3/4;
hold U to unbind. Existing quiet plus two-second preparation rules apply.
L toggles the selected bound Torch or Lantern. The HUD shows selected fuel,
lit state and the physical binding mask. Storage, drop and cargo controls remain
as described in the earlier checkpoint.

## Native verification

The current two-owner fixture passes 32 checks: original-instance pickup, quiet
binding, duplicate toggle replay, no passive stats or extra sight, Blind,
Mute rejection, lit unbinding/drop, denied ground activation, no automatic fire,
repickup and accessory swap, actual end-life and life return, and final expiry.
The expiry setup consumes remaining fuel down to 1,400 ms and then waits on the
normal clock. It never refills an item, creates a new tool or accelerates the
gameplay clock. A single GUID's fuel trace stays nonincreasing from 180,000 to
zero. The expanded world also passes all 49 storage/cargo regression checks.
The 75 structural checks, all 52 interior/stair walks, independent two-client
door regression and all 51 rules tests pass. Editor and Game targets compile
with full unity. All 148 generated B90 assets and 59 supplied files retain their
hashes. Import suites pass 5 tool, 4 object, 7 structural, 13 v0.9 and 9 v0.8 tests.
This closes T06's retained-fuel/cosmetic-light boundary; optional socket fire
and the other functional-environment cases retain their separate acceptance.

| Native point | Remaining Torch fuel, ms |
| --- | ---: |
| Initial / pickup / first binding | 180,000 |
| Lit | 179,507 |
| Unbound while lit | 175,269 |
| Dropped while lit | 174,634 |
| Recovered and moved to accessory 2 | 172,023 |
| End-life drop | 172,023 |
| New life, after return and quiet | 155,679 |
| Repicked up | 155,055 |
| Diagnostic remaining-fuel burn | 1,400 |
| Actual expiry | 0 |

```powershell
python -B Scripts/SyncB91Tools.py --check
python -B Scripts/TestB91ToolsImport.py
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Tools -Port 7923
& Scripts/RunB91Network.ps1 -Objects -Port 7921
```

This is editor-server evidence with two independent headless clients. It does
not establish rendered acceptance, packaged dedicated-server support, human
pacing or performance stress. Ladder/rope links, socket fire, saplings, grass,
furniture, all 3,000 scenery trees and full-map paid fields/contact remain open.
