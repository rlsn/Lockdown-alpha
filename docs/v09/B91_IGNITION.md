# B91 — finite Torch socket ignition

28 October 2026 · source 37fa1ab3d7501e83a17c4f751959398852bf0269 · B91 remains in progress

Bound Torches now ignite an observed living sapling or existing Oil field through
a one-second held item action. It requires six combat-quiet seconds, current
body sight within 120 cm, an actual accessory binding, at least 1,000 ms of fuel
and accepted field capacity. Success spends 1,000 ms of retained fuel and starts
a 12-second owner-family and physical-instance cooldown. It creates at most four
supported 200 cm Burning cells for three seconds, with normal periodic surface
damage and original owner/life attribution. It never creates a direct hit or
direct-Frost Stun.

The profile remains `v09_environment` on `L_V09Full` with `LDB91GameMode`.
Initial sources and endowments remain 384/480 fitted functional sources, 328
object/anchor records and 132 physical items. No population, markets or Heart
victory executor is admitted. Grass ignition and broader fire spells are still
open; scenery never becomes an implicit source of fuel or drops.

## Item channel and field reservation

The new `Ignite` verb extends the existing world request/receipt dispatcher.
Its category remains an item action: Mute, protection, Stun/Hex, ownership,
binding, readiness and held-input checks apply. Existing verb values are stable.
The request carries match, life, sequence, contract, current inventory/item
revisions and, for saplings, the current observed object handle/revision.

Starting reserves the actual Torch and exact eligible field count before any
ignition fuel or cooldown is charged. Unbinding, dropping or transferring that
reserved instance cannot bypass the channel. The server refreshes its internal
revision stamps after reservation and continues validating them. Releasing,
moving, taking damage, losing sight/range, suppression, life change or stale
heartbeat cancels and frees both reservations. Continuous light fuel, if the
Torch is lit, still follows its existing clock; cancellation refunds no elapsed
illumination time.

The Torch can ignite while unlit; the explicit one-second fuel charge still
applies and does not toggle its cosmetic light. A Lantern or Rope cannot use
this action. Remaining fuel is never refilled by ignition, cancellation,
binding, replay, drop or life return. The family endpoint belongs to the owner;
the physical endpoint travels with the same item.

The field lease reserves match, sponsor and transition capacity for the one-second
channel. Other casts count that held capacity. Release revalidates dry support,
the original target/source and current surface eligibility, then queues the
reserved cells before applying the cost. Water/ice, conflicting support,
expired Oil or a changed socket can refuse release without ignition cost.
The one-active-support-plane restriction from the paid-field checkpoint remains.

## Finite source state

Only an actual authored sapling supplies flammable socket cells. The source's
own trunk is ignored for the short footprint ray; other blockers and support
discontinuities still clip the footprint. Ordinary scenery, fixed furniture,
empty barrels and a lit dropped Torch supply no fuel.

A successful sapling ignition records one source receipt, publishes a burning
cue and schedules clearance three seconds after release. Its walking/sight
blocker remains until clearance. It cannot be harvested or ignited again.
Clearance sets HP to zero and removes collision, sight and local-navigation
proxies with one shared revision; no Fiber or other reward is created. Ordinary
melee can clear the already committed source earlier without yielding material.

The committed source exhaustion survives caster death and same-world warm
Initialize. Extinguishing a temporary field cannot restore a spent sapling or
its harvest opportunity. Cross-process saved-match restoration is not yet
implemented or claimed.

Oil is an existing paid surface, available to any currently authorized quiet
igniter. Torch ignition consumes that overlay into three-second fire and credits
the igniter, while the unchanged non-Torch Oil reaction retains its six-second
duration. No adjacent source is automatically ignited and no reaction output
recursively creates more fire.

An inherited timing constraint remains: an Oil Flask lasts six seconds, while
its release resets the user's six-second quiet timer. The native encounter uses
one owner to pour paid Oil and a quiet rival to ignite it. This checkpoint adds
no quiet-time exemption for a solo Oil→Torch sequence. Assess that constraint in
play before changing the established rules.

## Observation and controls

The existing seven-field object view adds a burning state bit; it exposes no
source receipt, exact clear time or caster identity. Existing three-field surface
views show only observed position, revision and states. Their sight filtering,
life fences and expiry are unchanged. Precise fuel and cooldown remain private
to the item owner. Light still grants no sight, detection or passive stats.

Select a Torch with **I**, prepare it with held **1–4**, then hold **R** near a
sapling. With no nearest sapling selected, R uses Oil ground at the cursor.
Release, movement or Escape cancels. The keyboard path and primitive color cue
are development UI; this evidence does not validate rendered readability,
context selection among crowded props or the complete click-to-approach loop.

## Native evidence and remaining work

[The audit](evidence/B91Ignition/Audit.json) retains source hashes, received
client reports and raw logs. The eight-phase scenario passes 42 checks with two
independent headless remote owners and actual clocks. It covers unbound and
Mute refusal, real quiet binding, held cancellation/replay, a one-winner sapling
race, caster death during committed fire, three-second clearance, actual life
return, warm source exhaustion, paid Oil ignition and saturation during an
accepted reservation.

Setup transfers two existing Torches and one existing Oil Flask. Recovery
explicitly adopts the same dropped Torch after actual life return. Capacity
setup uses supported diagnostic Water cells under detached sponsors; it proves
allocator behavior, not 1,024 normal earned actions or B94 performance. One Oil
consume leaves 131 conserved physical items; fire creates no material reward.

The current-source bundle also reruns paid fields, saplings, furniture, storage,
traversal, fuel, structural walks, independent doors, native rules and legacy
field/source-accounting regressions, plus full-unity Editor and Game builds.
All 148 saved assets and 59 supplied files retain their hashes. Earlier evidence
remains frozen.

| Verification | Result |
| --- | --- |
| Ignition | 42 native checks; two independent owners; 131 conserved physical items |
| Rules | 54 passed; includes fixed Torch duration and unchanged non-Torch Oil reaction |
| Existing B91 cases | Fields 33; saplings 28; furniture 51; storage 49; traversal 55; fuel 32 |
| Structure/doors | 75 checks, 52 actual walks with zero falling; independent door race/privacy scenario |
| Legacy fields | 536 server and 40 checks per client at 100 ms RTT / 10 ms jitter |
| Builds | Full-unity Editor and Game Development |
| Preserved inputs | All 148 saved assets and 59 supplied files retain their hashes |

```powershell
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Ignition -Port 7947
& Scripts/RunTests.ps1
& Scripts/BuildGame.ps1 -FullUnity -MaxParallelActions 4
```

T10 remains partial: ranged/spell sapling damage, saved-match restoration and
the complete composite encounter still need evidence. T11 grass/concealment,
tagged ledges, broader full-map reactions, paid Frost acquisition, layered
fields and density fitting remain open. Ninety-six grass sources and 3,000
scenery trees are still unfitted. Expanded field limits require B94 stress;
human, rendered and packaged-server acceptance remain separate gates.
