# B81 — native mixed loadouts and intrinsic traits

26 October 2026. Builds on `aeffed2` (B80). B81 implements an explicitly
selected **v08_integration development profile** on the existing native game.
B82 river core is next. This is a bounded engineering milestone; B82–B86 and
the inherited release deferrals remain open.

## Delivered behavior

- Six typed shared actions: Learned(skill) or Gear(instance, definition).
  Head/torso/legs/feet/gloves/main hand/off hand and four physical accessories
  share the existing 24-instance bag. Two-handed equipment reserves both hands
  and counts once. Canonical and owned-view binding masks now use 16 bits.
- Body activations require an equipped source **and** a shared action slot.
  Accessories activate only from their own binding. No separate quick bar,
  backpack Use shortcut, passive accessory button or independent signature
  action exists in v08. Legacy bag Use/Place/Bind/Unbind RPCs cannot bypass the
  versioned source path.
- Authority derives the source category from its own loadout. Silence blocks
  all learned actions, including legacy Step. Mute blocks body and accessory
  actions. Stun/Hex block both through the retained effect resolver. Breaking
  tagged equipment behavior does not remove an intrinsic trait or grant access
  to an unallocated body action.
- Full drafts include build/inventory revisions, six typed actions and eleven
  bindings. Station preparation validates the projected state and publishes
  equipment/actions once after the existing quiet/stationary hold. Item source
  removal clears dependent actions in the same transaction. Reservations,
  equipped trade/recipe exclusion, weight and natural prerequisites remain
  enforced. Source removal and replacement never reset cooldowns.
- Consuming one bound consumable clears that binding and consumes its original
  identity. Spare instances remain unbound; there is no refill. Bound consumables
  and objective relics drop on death; ordinary bound equipment retains. The
  existing explicit return refills HP/MP and retains absolute learned, instance
  and owner-family endpoints. Increasing maximum resources through equipment
  does not refill them.
- Primary allocation follows all eleven bindings in head-first order with
  +2/piece/attribute, +3/attribute and +6 total caps. The preparation preview
  shows the full projected equipment and suppressed excess. Body-source choices
  can equip the source and allocate an action in one draft.
- Native HUD shows six source badges and four physical accessory cells, with
  ready/cooldown/Silence/Mute reasons. Passive accessories have no action button.
  Q/W/E/R/D/G and accessories1–4 use saved collision-checked input mappings;
  A/F/T/S retain their meanings. Migration uses separate v08 preferences,
  preserves valid v07 assignments and relocates conflicting new bindings.
  The lobby refreshes its preset list when the owner profile arrives.

## Traits and enabled content

One trait is derived from the immutable match preset; there is no second trait
field for a client to stack or respec. Nine authored class mappings preserve
Laborer/Rogue/Scholar starts and six teaching presets. Native hooks implement
Laborer capacity, Vanguard Brace arc, Infiltrator/Rogue clue lifetime, Seeker
capped sight, Broker strongest assembly discount and Trickster decoy duration.
Arcanist/Scholar elemental lifetime rules share the 10s cap; their actual
element-field application awaits B82/B83's field service. B81 does not claim
observable Frozen/Burning/Electrified fields from that hook alone.

The enabled slice has **27 retained abilities, 54 items and eight playable
presets**. The 14 additions are scalar head/legs/gloves/weapon/focus/armor,
movement equipment and existing protection/cleanse operations; the exact IDs
and hashes are in `Content/Lockdown/Data/V08/B81Manifest.json`. Each enabled
definition retains its authored paid/recipe/drop policy. New priced equipment
uses real official supply and source/sink transactions; eligible quality uses
the same existing fee/input/identity transaction and source whitelist.

Preset5 (Arcanist) remains unavailable because its required Frostline starter is
not implemented yet; Arc Bolt is not silently substituted. The remaining 25
abilities, 26 items and eight new recipes stay outside the enabled slice until
their handlers and dependencies are implemented. Ember Line retains its v07 damage zone in the v07 profile; its added v08
Burning surface is not implemented, so the action is hidden in v08 until B83.
Existing four-route native
world behavior remains available in v07; the v08 B81 slice uses the retained
development region. New river/world integration belongs to B82–B85.

## Contracts and preservation

`Scripts/SyncB81Data.py` verifies adopted provenance and the supplied structural
schema before generating separate schema8 `combat_b81.json`,
`equipment_b81.json`, the enabled manifest and compiled digest header. Runtime
catalogs validate the compiled payload hash and derived fields. Schema8 never
passes through the closed schema7 importer. Both UFS payloads are explicit module
dependencies; runtime does not read docs.

Wire4/schema8 adds bounded source metadata; build drafts add fixed six-action /
eleven-binding records; loadout version2 permits the eleven bindings. Server
validation rechecks match/life/sequence, revisions, digest and source identity.
Accepted duplicate sequences return cached receipts without another spend.
The legacy v07 schema7/wire2–3, five-slot build and version1 loadout branches
remain isolated. All clients need the current native modules; no compatibility
with an older executable's reflected owner structs is claimed.

The B80 full disabled inventory, adapter reservations, input manifest, original
archives/data/templates and B80 evidence remain frozen. Its checkpoint checker
is expected to reject later runtime changes; use the B81 sync check for the
current enabled payload. No existing numeric skill/item IDs are renumbered.
New item enum members use B80's reserved values.

## Verification

Native Win64 Development Editor build passes on UE5.8.4 CL58902052,
MSVC14.44.35229 / SDK10.0.26100.0, max4 compilation actions.
The **41-test native rule suite passes**, including the positive six-learned /
four-accessory limit, invalid sources/shapes/revisions, two-handed conflict,
primary caps, trait rules, saved-key collisions and typed wire round trips.

The focused 100ms / ±10ms native server + two independent clients exercises real
RPCs and held commits, source replication, Silence/Mute, paid supply, emptying a
consumable binding, stale/unbound source rejection, rebind and death/return.
Setup uses labelled diagnostic item/wallet funding, quiet/position/control and
return-clock changes. It is not a normal earned progression journey, a physical
input test or human review. The native preparation widget is also opened against
the replicated typed owner draft. Results and scope are recorded under
[evidence/B81](evidence/B81/Summary.json).

The retained v07 B73 loadout network fixture passes separately: 46 authority,
27 client A and 26 client B checks. The fixture ledger is independently replayed
with `CheckEconomyEvidence.py`; v07 data imports and the v08 specification model
checks pass unchanged. The supplied model checker still reports specification
evidence only; native results belong to the separate working ledger.

Development corrections are explicit: initial compilation exposed include and
legacy-validation placement mistakes; the first client run caught a HUD read
of eleven bindings during the six-binding lobby state, now guarded; a subsequent
fixture completed its clients before its server read, now handled without
dereferencing disconnected controllers. Failed runs are not counted as passes.

```powershell
python -B Scripts/SyncB81Data.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/RunTests.ps1
./Scripts/RunB81Check.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunLoadoutCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/OpenStandalone.ps1 -Profile v08_integration
```

No fresh packaged Game build, rendered-resolution/layout matrix, physical
keyboard/mouse run, human session, long soak or full cross-feature privacy/race
matrix was run. The existing V07-D01–D08 / V06-D01–D08 deferrals remain named.
The working ledger retains partial/not-run status beyond this focused evidence.
