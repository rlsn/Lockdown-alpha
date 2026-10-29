# B91 — functional environment completion review

29 October 2026 · **Complete at the functional-environment boundary**

Native source: `8c591a686b6dc91a4db3ae2e4c9a03cce447f746` · [Frozen evidence](evidence/B91Completion/README.md)
· [Audit manifest](evidence/B91Completion/Audit.json). All21 final native matrix
entries pass, and the portable evidence/conservation checker passes.

B91 delivers the full-map functional environment in `v09_environment`, using
`L_V09Full` and `LDB91GameMode`. This report consolidates the earlier bounded
increments; their reports and frozen evidence remain historical. Completion of
this stage establishes environment behavior and its authority boundary. B92 owns
normal acquisition, living population and full-map economy; B93 owns earned
victory integration; B94 owns full sessions, human pacing and performance.

## Delivered world

The world contains 36 enterable buildings out of 144, 72 authoritative doors,
16 permanent stair/optional-link pairs, 480 supplied functional sources plus the
11 user-approved Forestry Knives, and 3,000 fixed scenery trunks. There are 435
initial object/anchor records and 143 initial physical goods; door records and
anchor aliases explain the difference from the functional-source census.

The forest uses one nonreplicated, nonticking instanced actor. Twelve supplied
forest patches each fit 250 trunks, preserving functional pads, approved roads,
service and spawn clearances, traversal endpoints and Heart approaches. Trunks
block walking and sight and contribute to native navigation. They have no item,
harvest, ignition socket or loot authority. They are coarse review geometry;
final forest art and performance acceptance remain later work. The engine default
material supports instancing without resaving a protected game material.

Riverport's existing `prop_crate_002` is fitted into a fixed warehouse alcove at
building-relative (-350, 500, 0) cm. Its authored source, physical contents and
finite identity remain unchanged. This exact pad is an authored exception to the
generic no-placement-in-building envelope; full support, footprint, collision
and occupancy checks still apply. Arbitrary interior cargo placement stays
forbidden. Checkpoint validation uses the same approved pad. Permanent fitted
floors become static after construction, so character corrections use world
positions rather than unresolved server-only dynamic bases.

## Interaction and information contract

The [user control decision](CONTROL_POLICY_OVERRIDE.md) takes precedence over the
original F/Z/R and free lockwork design. RMB orders ordinary pickup and doors;
LMB opens storage and traders. Harvest/chop requires an existing bound Forestry
Knife; ignition requires a bound fueled Torch. Normal attacks can break eligible
objects. An eligible POI can activate automatically. Channels begin with one
confirmation, show private progress, and stop through S, movement, damage,
control, focus or lifecycle interruption. No physical button hold is required.

A visible sapling outside item reach now supports one source confirmation that
walks into reach before starting the authoritative channel. Navigation targets
the object's supported floor instead of the elevated trunk proxy center. No
item reservation, fuel expenditure or harvest occurs during the approach.
Pointer/source continuations wait for the server arrival acknowledgment before
starting; crossing the predicted120cm boundary cannot race the server pose.

Original B91-T03's free rear-door force/lockpick actions are superseded by that
user decision. Current native tests require those source-free requests to fail,
retain exact resident lock ownership and public-front lock refusal, and check
broken-open collision with explicitly labelled state fixtures. No authored
breaching item has been selected. Historical positive free-force evidence is
not current gameplay acceptance.

Only authorized body/object/surface snapshots reach ordinary observers. Losing
observation revokes unit handles; stale commands cannot spend for a hidden unit.
Private inventories and container contents remain on their owner/quote paths.
Fixed forest geometry is public and carries no mutable hidden state.

## Terrain and paid environment actions

Full-map prepared actions include Water Jet, direct Frostline, Firebolt, Gust
and Smoke Veil; supported bound catalog items include Water/Oil Flasks, Frost
Capsule/Prism, Gust Vial, Smoke Flask and Seer's Dust. Normal source, rank,
attribute, binding, Mute/Silence, MP, cooldown, version and field-budget checks
remain authoritative. Combat fixtures label their prepared builds and catalog
items; these tests do not demonstrate B92 acquisition.

A supported dry-floor Frostline still spends normally and damages a body without
inventing Frozen cells, Stun or a reaction-lock refresh. Surface-only item casts
retain strict outcome admission.

Native cases cover river Wet, 600 ms direct-Frost Stun, the 6,000 ms victim
reaction lock, Control Guard, actual death/return, Frost Capsule's floor slow,
Steam union occlusion, a stale unit request and Gust clearing. Ground casts use
physical geometry to aim into a cloud so Gust can clear it; unit targeting still
requires current observation. Narrow full-map beams select touched cell footprints
so a beam between cell centres still affects its floor. A direct Fire hit at a
cell corner can use the incoming and containing contact cells, within its paid
two-cell reservation and the same support/geometry checks. The inherited small-map
mask stays unchanged. A real consumed Seer's Dust creates the detector;
geometry and actual expiry govern its grass authorization. Wet and dry Heart
approaches use the same environment rules without granting claim admission.

Directional ledge tags are deterministic rings halfway across authored plateau
aprons (plateau radius + 30 m). Approved road/trail aprons open 60 m ramp gaps in
those rings. A ray crossing inward with at least 1 m of supported terrain relief
is blocked from below; a supported source above the ring keeps normal downslope
sight subject to walls, trunks, smoke and range. Same-band rays and elevated
roof sources retain normal geometry. There is no range or damage bonus. This
adds a semantic sight edge to the existing continuously traversable side slopes;
it does not change the protected saved terrain assets.

## Connected encounter and recovery

The reference fixture transfers three existing tools into the starting bag and
stages the initial two bodies once. Thereafter one remote owner walks from the
public frontage around to the rear, opens it with RMB, uses the indoor warehouse
crate with LMB, stores and takes the exact Knife, binds and installs the original
Rope, traverses the roof link, returns on permanent stairs, retrieves the Rope,
confirms a distant sapling harvest once, and escapes into grass. The peer receives
only its restricted view. Tool staging is explicit; movement and transactions
use ordinary authority paths.

The same journey then spends Torch fuel to burn the escape patch, retires its
owners through normal property handling, and saves the settled world. A fresh
native process restores those actual harvest/burn receipts, reward and original
tool identities, locations, door state and spent fuel. It cannot restore twice.
The separate corruption suite checks rejection before mutation.

Checkpoint scope is a settled, ownerless environment. Full personal accounts,
rosters, economy and active-match recovery are later integration work. Operator
files are trusted; there is no anti-rollback guarantee. Offline time pauses the
saved remaining timers. These limits are distinct from a fresh-match reset.

## Acceptance evidence

The final matrix is recorded in `Artifacts/20261029-172604-761-B91-CompletionRun`.
Portable records live in [evidence/B91Completion/](evidence/B91Completion/README.md),
pinned to the native source above. Source/Config remained fixed throughout the
final native matrix. Coverage follows the current user control policy below.

| Case | Evidence boundary |
| --- | --- |
| T01 | All36 interior passages and16 permanent-stair ascents; closed shells remain closed; collision/window checks |
| T02 | Two remote owners, door travel/race, occupied closure, shared proxy revisions and hidden/reobserved snapshots |
| T03 | Superseded source-free lockwork; current requests reject; resident/public-front rules and labelled broken-state geometry retained |
| T04 | Existing flask and ladder races, exact identity and one winning transfer |
| T05 | Bound Water/Oil actions, unbound/Mute refusal, consumption, cooldown and private field views |
| T06 |32 tool checks through binding, drop, swap and actual life return; checkpoint fuel/timer retention |
| T07 |49 storage/cargo checks,70%carry speed, safe placement, swept push and no displacement; checkpoint footprint validation |
| T08 | Existing filled/empty container attack races, one drop per stored identity, no invented contents |
| T09 |55 traversal checks, all16 paired routes, both directions, reservations, Root/Stun, life and safe return |
| T10 |53 source/weapon checks plus the connected earned harvest and fresh-process reload; one Fiber per source, destruction none |
| T11 |44 grass checks plus real consumed detector/expiry and the connected escape/burn/reload |
| T12 |50 combat checks include Wet/direct Frost, reaction endpoints, Guard, actual death/return, floor slow and dry-floor damage |
| T13 | Shared Steam occlusion, stale observed-unit refusal, paid Gust clearing, wall/detector restrictions and restricted wire schemas |
| T14 |33 paid-field and42 ignition checks,1024-cell/64-owner/2048-transition caps, reservations, no-cost refusal and exact expiry/fuel |
| T15 | All491 current functional sources,3,000 fitted fixed trunks,6,000 native blocker rays and864 spawn/service-front paths |

The structural run passes75 checks and52 native walks in267.217 aggregate
simulation seconds with zero falling. The door network run passes19 checks.
The rendered connected encounter adds44 checks,231.342m of actual movement,
zero falling and zero explicit move retries, plus a separate three-check earned-world
restart. The corruption/placement checkpoint suite adds59 checks across four
processes. The native rule suite has56 passing cases; inherited fields retain616
assertions and an independently replayed conservation ledger. These are fixture
counts, not human playtest or full-session claims.

Captures show one remote owner rendered at1280×720 with a headless peer and
reduced DX12 settings (75% internal screen percentage; shadows/GI/reflections
reduced). Roof, source-channel and grass images are inspected. No frame-rate,
latency percentile, hardware stress or human readability acceptance is inferred.

Failed attempts remain diagnostic: invalid fixture attributes, a mistaken reusable
Prism assertion, missing standalone includes, the initial warehouse lookup,
MP-regeneration equality, narrow-grid surface admission, projected-waypoint
acceptance, low-memory rendering, and the predicted-range/arrival race. The final
source addresses the actual defects; setup/checker corrections remain labelled.
The first final-evidence wrapper also rejected UTF-16 legacy JSON; the reader now
detects its BOM and the unchanged native evidence passes.
The storage checker validates only the exact selected warehouse exception and
keeps the generic10m bound for every other source.

Run native builds/scenarios strictly sequentially with Source/Config fixed. The
new runners are `RunB91Combat.ps1`, `RunB91Encounter.ps1`,
`RunB91Forest.ps1` and `RunB91ForestRoutes.ps1`. Existing structural, source,
storage, tool, traversal, ignition, grass, checkpoint and inherited regression
runners remain the acceptance paths.


## Reproduction and next stage

~~~powershell
./Scripts/RunB91Completion.ps1 -MaxParallelActions 2 -StopOnFailure
python -B Scripts/ExportB91CompletionEvidence.py <run-folder>/Runs.json --check
python -B Scripts/CheckB91Completion.py docs/v09/evidence/B91Completion
~~~

The matrix builds both Development targets with adaptive unity disabled and runs
native scenarios sequentially. A frozen export requires committed Source,
Config, Scripts and generated V09 data. Keep later failed attempts separate from
this accepted bundle; do not rewrite historical evidence.

The [master reference](../master/README.md) is revision20. B92 is the next stage:
initialize and conserve the five market pools and official stock, connect normal
acquisition/work/finite sources, establish the full roster and living routines,
and add locally witnessed security and legal contender AI. B93 earned full-map
victory and B94 full-session/human/performance acceptance remain open. Those are
separate from the completed B91 environment and its settled-world recovery scope.

[Roof capture](evidence/B91Completion/encounter/Reference_15.png) ·
[One-confirmation harvest](evidence/B91Completion/encounter/Reference_22.png) ·
[Grass escape](evidence/B91Completion/encounter/Reference_24.png).
