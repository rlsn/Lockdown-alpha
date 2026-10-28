# B91 — authored grass and concealment

28 October 2026 · native source `138fe1b58cc6b36f1ef9b369dfd818dc41fab358` · B91 remains in progress

The 96 authored grass sources now use the existing B91 object and sight
authorities. They create no item, harvest reward, keyboard binding or prepared
action. Grass is a 250 cm feet-point disk on its fitted supporting floor,
represented by a 500 × 500 × 150 cm nonblocking placeholder. Its proxy blocks
neither walking nor sight and does not change navigation connectivity.

## Player behavior and information

Entering live grass begins one second of rearming. Movement inside the disk can
remain concealed; leaving it removes that source immediately. A released basic
attack or cast reveals its source for two seconds, followed by one further
quiet second before grass concealment returns. Active casting refreshes that
window until it stops; a long cast cannot wait out its own reveal. Quiet
nonoffensive inventory preparation does not remove natural grass cover. Interrupted concealment and
positive damage also restart rearming. A new life receives a fresh eligibility
state, rather than inheriting a previous body's timer.

Concealment is evaluated separately for each observer. An ordinary observer
within two metres may see a grass occupant through valid geometry. An authorized
detector may also reveal the occupant. Both still require sight through the
same walls and smoke/steam boundaries. The proximity exception applies only
to grass: it does not reveal timed invisibility or the Shadow Charm's separate
cover effect. No identity, build, wallet, source owner or private status is added
to the existing eight-field body view or seven-field object view.

Observation loss revokes target resolution as well as the visible proxy.
An old handle cannot authorize a hidden body. Grass cannot conceal an active
Heart claim; this profile proves that owner-state gate through diagnostic setup,
because full-map earned Heart execution remains a later-stage dependency.

## Finite paid clearance

A physically bound Torch can target an observed grass socket, using the existing
one-second channel, one-second fuel commitment, six-second quiet requirement,
Mute gate and twelve-second family/instance cooldown. Confirmation is a single
order, with progress and S cancellation. Two owners competing for one source
can produce only one committed ignition; replay cannot spend a second cost.

Ignition immediately disables the patch's concealment and commits up to four
reserved Burning cells for three seconds. At the source's three-second endpoint,
its visible proxy clears with matching collision/sight/navigation revisions.
The named patch stays burnt for the match. No fire spreads to another patch,
no Fiber is created and extinguishing temporary fire cannot restore committed
cover. Warm initialization and real life return preserve the mutation;
cross-process saved-match restoration remains open.

The additive compiler reads the frozen props, object-types and interaction
rules and rejects changed counts, duplicate IDs, altered timing, solid grass,
boolean coordinates or an added reward field. Its contract hash extends the
existing control contract. All 148 saved assets and 59 supplied release files
retain their original bytes.

The initial world now has 435 object/anchor records and 143 physical items.
All 491 functional sources, including the eleven approved Forestry Knives, are
present at their documented behavior scopes. This source census does not close
broader sapling actions, saved mutations or the 3,000 scenery-tree fitting gate.

## Evidence boundary

[Frozen evidence](evidence/B91Grass/README.md) pins committed native inputs,
checksums and raw two-owner snapshots. The grass scenario covers all 96 fitted
sources, hiding without occlusion, clear two-metre observation, wall refusal,
detector gain/loss, stale handles, real movement inside cover, an ordinary attack,
reveal/rearm endpoints, exit/re-entry, binding, paid ignition races and real life
return. The wall, detector, sustained-cast and claim-state setup are explicit diagnostics.
Detector acquisition is not claimed as normal B91 progression.

Companion regressions include the source/pointer scenario, sapling/Oil
ignition, storage/furniture, native rules and inherited field workload with
independent ledger replay. Editor and Game use full-unity Development builds.
The single 720p offscreen rendered owner supplies a visually inspected frame;
the other owner is headless. This is not human input, performance, complete
Riverport encounter or whole-stage acceptance.

| Verification | Result |
| --- | --- |
| Final rendered grass scenario | 44 native checks, two restricted owners |
| Source/pointer interactions | 47 native checks |
| Sapling/Oil ignition | 42 native checks |
| Storage/furniture/cargo | 51 checks, 435 fitted records and 143 physical items |
| Native rules | 55 pass |
| Inherited field workload | 616 assertions; 414 receipts independently replayed |
| Grass import boundaries | Six checks, plus inherited import suites |
| Builds | Editor and Game full-unity Development targets pass |

Companion fixtures were run during the increment. The final 44-check rendered
scenario and inherited field workload include the sustained-cast timer fix;
the final builds contain all changes. Fresh source discovery also exposed six
pre-existing primary-header include-order errors from the liveness helper;
those translation units now include their matching header first.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
./Scripts/RunB91Network.ps1 -Grass -Capture -RenderedClient A -Port 7971
./Scripts/RunB91Network.ps1 -Saplings -Port 7972
./Scripts/RunB91Network.ps1 -Ignition -Port 7973
./Scripts/RunB91Network.ps1 -Furniture -Port 7974
./Scripts/RunTests.ps1
./Scripts/RunB84Check.ps1 -Port 7975
./Scripts/BuildGame.ps1 -FullUnity -MaxParallelActions 4
```

Next work is persistent mutation restoration, scenery fitting, broader paid
actions/reactions, layered fields and the complete reference encounter. B92
population/economy and B93/B94 route/session acceptance remain separate gates.
