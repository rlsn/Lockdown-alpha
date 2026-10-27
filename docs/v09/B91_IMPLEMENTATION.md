# B91 — functional environment

This is the historical structural/door checkpoint. The later
[finite storage/cargo](B91_OBJECTS.md), [portable fuel](B91_TOOLS.md) and
[approved traversal](B91_TRAVERSAL.md) reports record later object progress;
each evidence bundle preserves the source it actually tested.

Native source: **a5fc200df683f3a04054ad58cdf2d3c9fecf54dc**, based on the
B90 completion at **3358cfc**. Compiled structural contract:
`4137f1de587c7f9577565d218ecfdf02de85711859ff8399282320c159dd9bfc`.

B91 is **in progress**. This increment establishes the full-map interaction and
observation boundary, selected structural interiors, permanent stairs and real
door transactions. It does not close the entire functional-environment stage.
The [B90 completion record](B90_COMPLETION.md) remains the spatial baseline.

The [portable evidence](evidence/B91/README.md) and [audit](evidence/B91/Audit.json)
record the exact source, compiled contracts, native reports, traces and client
videos. This increment covers B91-T01–T03; B91-T04–T15 remained open at this source.

## Architecture and profile

The explicit `v09_environment` development profile runs on `L_V09Full` with
`LDB91GameMode`. It reuses the inherited contender/life state, combat control
gates, native navigation orders and CharacterMovement. It starts no legacy
residents, markets, resource endowments or Heart victory executors. The existing
`v09_full` B90 standalone review and `v08_integration` remain separate profiles.
This is an environment implementation profile, not B92 full-match admission.

The structural compiler binds the 72 authored door IDs to native jambs, 20 cm
inside the semantic shell boundary. Both source and fitted positions are retained.
It preserves resident lock ownership and compiles the 16 selected permanent stair
buildings. Runtime refuses a mismatched compiled payload before admission. The
supplied manifests and B90 saved map packages remain unchanged.

The legacy 60×40 fog wire format is unchanged. Full-map observation uses a
separate, bounded owner RPC snapshot with local opaque handles and public door
addresses. A snapshot contains only currently observed door revisions and body
presentation. It contains no remote registry, private inventory, wallet, class,
human label or resident identity. Losing observation removes body proxies;
unobserved doors retain their last known presentation instead of receiving live
mutations. Interaction resolution rechecks current geometry before using a
previously observed handle.

## Structures and doors

Only the explicit environment profile replaces the 36 selected shell wall sets.
Each has two physical doorways and two authored sight windows. A window's sill
and lintel reject a full capsule while its opening admits an eye ray. Closed
shells retain their original collision and expose no door operation.

Sixteen interiors have a permanent upper floor and a 3 m stair flight with twenty
20 cm risers and 62 cm treads. The central ground-floor corridor stays clear.
The physical stair boxes retain their 20 cm steps. A continuous, walkable
underlay supplies a stable local navigation surface beneath those steps because
the saved full-map Recast mesh uses 40 cm voxels. This changes no movement speed,
body step limit or global nav settings. Render cutaways hide only upper-floor
presentation above the local body; collision and sight remain present.
These stairs require no portable item or ability. Portable rope/ladder anchors
and their reservations remain later B91 work.

A door starts closed and unlocked. Open/Close takes 300 ms; rear lockpick/force
takes 3 s and requires six combat-quiet seconds. The resident authority alone
may lock its exact rear; a contender cannot acquire lock ownership, and a public
front cannot lock. Lockpick unlocks; force leaves a permanently broken-open rear
for the current world. Occupied closure rejects.

Requests bind match, life, sequence, layout, compiled contract, observed handle,
object revision and verb. Holds require fresh monotonic input, stationary body,
current reach/sight and unchanged damage/control state. Completion revalidates
before changing the physical proxy, sight proxy and dirty local-navigation
revision as one transaction. Duplicate requests replay the original receipt.
Native collision stops a stale route while local Recast tiles catch up; door
changes do not request a whole-map navigation rebuild.

Applying an already-applied door revision does no physical or navigation work.
This matters on a rendered client: repeatedly dirtying the same doorway from
unchanged observation snapshots could keep its local route unavailable. The
native probe checks this explicitly. The remote fixture also records bounded
local-nav retries while the asynchronous tile catches up; it waits for the
actual Arrived acknowledgment before starting a stationary door hold.

Camera cutaways affect rendering only. Upper floors near the controlled body
and wall pieces intersecting its camera ray can fade without changing walking,
sight or navigation. Canonical nonowner body meshes stay hidden; only authorized
body proxies are presented, including in standalone/listen rendering. Concealed
or expired proxies are removed rather than retained as an unbounded hidden pool.

The development HUD exposes only implemented door verbs. RMB uses the existing
native navigation authority; S/Esc cancels. F holds Open/Close, V holds rear
lockpick, B holds force. These review controls are temporary; the complete
remappable context selector and production object inspector remain on the B91
UI integration list.

## Remaining stage work

The Riverport reference encounter still needs its actual warehouse storage/cargo,
portable traversal and grass escape. B91-T04–T15 remain open until the physical
portable items, accessory/fuel state, heavy carry/placement, containers, mutable
foliage, concealment/ledge rules, paid environment reactions, capacity reservations
and all 480 functional props / 3,000 scenery trees have native evidence. A
structural compiler or a world marker cannot pass those cases.

Restricted remote-client door mutation, race and payload evidence is separate
from local structural walks. Those door tests now pass with two independent
owning clients. Full-stage completion still requires the later B91 object cases;
B92 population/economy and B93 earned victory remain untouched.

## Verified results

| Check | Native result |
| --- | --- |
| Contract compiler | Seven rejection/reproducibility tests pass; 36 interiors / 72 door IDs / 16 permanent stair buildings |
| Builds / rules | Editor and Game Development targets build; all 51 native rules pass, including strict observed/public schemas |
| Structural walks | 36 ground-floor passages plus 16 stair ascents; 267.500 aggregate simulation seconds; zero falling |
| Door/window geometry | 72 closed-door capsule blocks, 72 closed-door sight blocks, 72 open-door capsule passages; 72 sight windows admit an eye ray and reject the body |
| Door authority | Stale revision/life, resident ownership, public lock refusal, quiet lockpick/force, occupied closure, replay, protection, Stun, Mute/Silence distinction and movement cancellation |
| Remote owners | Independent clients A/B; received observer-local handles differ; native movement through an opening and a subsequent Close |
| Remote privacy/races | Distant observer gets no door mutation; reobservation gets the current revision; occupied Close changes nothing; concurrent requests produce exactly one commit |
| Proxy agreement | Door collision, sight and local-navigation revisions agree; unchanged snapshots perform no proxy update |
| Regression | Native rule suite and actual B90 reload → v08 source-ledger replay retained in the evidence |

Both client views have timestamped native video. Each recording run contains two
real remote owners, with **one rendered client and one headless peer**; A and B
are recorded in separate runs. The 1280×720 captures use reduced shadows/GI/
reflections and approximately five screenshots per second, encoded with their
source timestamps. They are visual/authority evidence, not a frame-rate or
full-session claim. Simultaneous dual rendering exhausted the machine's memory
with both default and reduced settings; the audit records those failed runs.

## Reproduction

Keep Source/Config fixed within a native run and execute native operations
sequentially. The three-process network topology is one scenario.

~~~powershell
python -B Scripts/TestB91Import.py
python -B Scripts/SyncB91Data.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 3
./Scripts/RunB91Check.ps1
./Scripts/RunB91Network.ps1
./Scripts/RunB91Network.ps1 -Capture -RenderedClient A
./Scripts/RunB91Network.ps1 -Capture -RenderedClient B
./Scripts/RunB90Closure.ps1
./Scripts/RunTests.ps1
./Scripts/BuildGame.ps1 -MaxParallelActions 3
./Scripts/OpenB91.ps1 -Visible
~~~

Video encoding additionally uses `Scripts/requirements-b91-videos.txt` in a local
virtual environment or scoped runtime:
`python -B Scripts/EncodeB91Videos.py <captured-network-run>`.
Raw PNGs remain in local Artifacts; portable videos, timestamps and per-frame
hashes are retained with the evidence.
