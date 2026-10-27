# B91 finite storage and cargo

Verified 28 October 2026. Native source:
**3f3071370bf07598d8bdae1df5ec3578a53aadef**. Compiled object contract:
`4af9a146ac6315c86781e40844a782149e47b1b76d91af9d8380e430245a126e`.
The [audit](evidence/B91Objects/Audit.json) pins source, native runs, immutable
inputs and portable evidence. [Evidence guide](evidence/B91Objects/README.md).

This increment extends `v09_environment` on the saved `L_V09Full` map. B91 is
still in progress. The earlier [door/interior evidence](B91_IMPLEMENTATION.md)
remains a historical checkpoint with its own source and artifact hashes.

## Implemented behavior

The checksum-closed object compiler selects 48 common chests, 36 crates,
24 barrels and 48 inherited Water/Oil Flask instances from the supplied source.
Exactly twelve chests start with two Fiber and one Ore; all other storage starts
empty. That is 156 world objects and 84 physical item endowments. No market,
NPC, respawn reward, gold, rare objective or automatic barrel payload is added.

Storage uses existing `ALDItem` identities and the existing inventory authority.
An item has one holder: ground, a container or one contender bag. Containers have
24 slots and a weight limit of 60 in this prototype; those limits are explicit
implementation choices where the supplied spec requires capacity without
assigning a number. Stored items must be unbound and unreserved. Contents are
sent only to the legal opener, and the quote expires on revision, life, reach or
sight changes. Concurrent read-only opens are allowed; mutations reserve their
object and exact physical item.

Pickup/take/store use a 500 ms hold and six combat-quiet seconds. Open uses
300 ms without a quiet requirement. Bag drop uses a 500 ms hold without adding
a combat-quiet requirement. All holds revalidate match, life, sequence, layout,
contract, observed handle, object/item/bag revisions, capacity, sight and range.
Movement, positive damage, control, released input and stale heartbeat cancel.
Repeated requests replay their original receipts.

Crates and barrels remain world instances when carried. Their stored contents
add weight; admission uses natural capacity. One prop binds to one body, moves
at 70% normal speed and suppresses attacks, skills, item actions and services.
An explicit attack/ability request first attempts a safe drop, then still passes
the ordinary action/control checks. Damage and contender end-life also drop
cargo. If no safe pad is available, the instance is retained and later drop
attempts can retry. A disconnected carrier cannot make the object collectible
while its carried state is unresolved.

Placement takes 1.5 seconds. Push moves up to 150 cm over one second with swept
collision and repeated surface/occupancy checks. Crates and barrels break after
a three-second hold with the ordinary quiet gate. Breaking removes cover and
drops its actual stored instances once; it produces no extra resource or oil.
The common carry rule includes barrels even though the source object-type verb
list abbreviates barrels as push/open/break.

## Geometry and observation

Initial fitting searches a deterministic 100 cm grid within 10 m of each source
XY, using native floor traces and a maximum 25% surface grade. An upright prop
rests above the highest sampled corner. Road exclusion preserves the entire
authored road width and at least 400 cm from its centerline, plus the prop's
radius. Building footprints, door/stair/spawn/Heart/service pads, rivers, other
props and all character capsules are excluded. Blocking props preserve a 3 m
local gap. The actor, collision, sight and local navigation proxy share each
accepted mutation revision.

Object snapshots contain six approved fields: opaque observer handle, public
kind, observed position, object revision, public state and physical item
revision. They carry no source ID, canonical actor, holder, inventory, role or
private stats. Legally opened container entries use a separate owner RPC.
Canonical authority meshes are never an alternate visibility channel.

A broken object sends its observed cleared state so clients remove its collision.
If cargo moves away, an observer who can see its previous position may receive
an empty-space update there. That update reveals no hidden destination, carrier
or live contents and cannot authorize an interaction. Unobserved state stays
frozen. Existing dropped-item proxies follow their physical item after life
drops rather than leaving a collectible ghost at the initial source.

## Review controls

Launch with `Scripts/OpenB91.ps1`. Right-click moves; S/Esc stops or cancels.
Hold F opens nearby storage or picks up a ground item, T takes the selected
container entry, Y stores the selected bag item, C carries, J places at the
cursor, P pushes, X breaks, and G drops the selected bag item at the cursor.
Tab cycles container entries; I cycles bag items. Existing F/V/B doorway controls
remain available when a doorway is selected. These are development controls;
the source-aware production inventory/action UI is still a separate integration
requirement.

## Verification and remaining scope

All 49 native object checks pass. This includes two-owner pickup/take/carry and
filled/empty break races, duplicate receipt replay, private container access,
empty-ground privacy, native carried walking, occupied placement refusal,
70% carry speed, action suppression, actual combat damage, Stun, contender
end-life, a 150 cm swept push and barrel carry/place. All 72 door pads, 16 stair
landings, 24 spawns, 36 service pads, the Heart and 117 route corridors reject
cargo placement. All 84 initial physical IDs remain conserved.

The final structural regression passes 75 checks and all 52 walks (267.500014
aggregate simulation seconds, zero falling). The existing independent two-client
door scenario and all 51 rules tests pass. Both Editor and Game targets compile
with adaptive unity disabled. The 148 generated B90 assets and 59 supplied source
files retain their original hashes. Import suites pass 4 object, 7 structural,
13 v0.9 and 9 v0.8 boundary tests.

T07/T08 pass at this cargo/storage boundary. T04 passes the flask race and stays
partial until ladder ownership is implemented. Functional source coverage now
totals 228 of 480, including the 72 existing doors. The reproducible commands are:

```powershell
python -B Scripts/SyncB91Objects.py --check
python -B Scripts/TestB91ObjectsImport.py
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Objects -Port 7921
& Scripts/RunB91Check.ps1
& Scripts/RunTests.ps1
& Scripts/BuildGame.ps1 -FullUnity -MaxParallelActions 4
```

The two-owner fixture uses an editor server and independent headless clients.
It does not establish a packaged dedicated server, video, human pacing,
stress/performance or whole-stage acceptance. Diagnostic setup transfers two
existing cache instances into a crate for the filled-container break race; it
creates no item. Source/end-state identity censuses make that setup reviewable.
The positive push fixture refits the same existing crate to a native clear pad;
its original generated pad remains in the manifest. It does not relax ordinary
placement rules. The carried walk returns with a margin for native navigation's
arrival tolerance before attempting to place the crate.

Ladder/rope ownership and links, portable Torch/Lantern fuel, binding/action UI,
saplings, grass concealment, all 480 functional props, 3,000 scenery trees,
tagged ledges and full-map paid field/contact integration remain B91 work.
B92 economy/population and B93 earned victory integration remain separate.
