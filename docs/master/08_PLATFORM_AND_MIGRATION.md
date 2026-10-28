# Platform, architecture and continuity

## Native foundations

The selected platform is Windows-first Unreal, a 3D world and controlled top-down camera. The inspected engine is pinned to Unreal 5.8.4 CL 58902052. Keep the working C++/UMG/Enhanced Input architecture and saved native geometry. No migration to GAS, World Partition, PCG or another UI stack is implied by map expansion.

Existing foundations include native character movement, authoritative navigation intent, owner/life identity, filtered world views, combat/effect scheduling, physical inventory, ledger transactions, institutions, route leases and native panels. Extend these services rather than introducing competing versions of damage, ownership, targeting or victory.

## Boundaries

| Responsibility | Owning boundary |
| --- | --- |
| Authored geography | Versioned definition/importer and deterministic native builder |
| Walking/collision | Native CharacterMovement, fitted geometry and validated navigation |
| Perception | Server authorization before serialization; same restrictions across UI/VFX/AI |
| Action legality | Existing request/hold/release path with life, costs, gates and cooldowns |
| Physical property | One instance identity and atomic ownership/reservation transitions |
| Economy | Explicit sources, finite budgets, transfers, burns and lineage |
| Outcome | One scheduler/event order and terminal receipt |
| Presentation | Native input/UI using authorized state |

B90 uses an independent full-map definition because the legacy region/grid format has small-map assumptions. Do not enlarge a wire field or fog allocation casually to make coordinates fit. Full-profile observation/replication requires deliberate B91–B94 integration and restricted-client evidence.

The initial B91 environment implements a separate bounded full-map observation
snapshot. Public door addresses accompany observer-local handles; other bodies
carry only authorized presentation. Current visibility is revalidated before a
handle can authorize an interaction. Last-known door presentation can remain
after sight is lost, while hidden mutations stay private. Unchanged observed
revisions do no collision/navigation work. This preserves both privacy and local
path liveness; the old fog wire format remains unchanged.

World objects use a separate bounded observed record and a private container
quote. A stable physical item has one location across bag, storage, ground or
installed use. A carried prop remains a world instance with one carrier, rather
than becoming a second bag item. Receipt replay, revision checks and reservation
ownership must agree across those systems. Rendering receives only current
authorized presentation or an observed empty-space update; a remembered source
cannot publish an item's later private inventory revisions.

The portable-tool overlay keeps the retained 80-item catalog and old item IDs
intact. New physical definitions use explicit append-only IDs and environment
admission. Continuous fuel derives from a physical clock; structural revisions
change on explicit activation/expiry, not every fuel tick. Draft commits and
transfers refresh the owner projection from that instance. Full-map sight uses
its own 18m base and the inherited Blind/bonus rules, independently of cosmetic
body/ground light. Legacy sight and fog formats remain separate.

Traversal reserves an approved anchor on the authority while retaining the
original world prop or installed physical item. Public endpoint geometry is
separate from private user/deployer state. Native body movement, manual capsule
occupancy and the owner's sequence-bound presentation must agree on the same
supported start/end points. Cancel before death drops or unpossession; resetting
an input reference must never duplicate the tool or leave an obsolete movement
order active on the next life. Saturated return pads require an explicit safe
pending state, followed by separate density stress validation.

## Static and mutable world

Keep object capabilities independent. A sapling blocks walking and sight without
being a container; fixed furniture blocks walking while permitting sight; cargo
adds storage and transport commitments. A collision flag must not silently
grant storage, destruction rewards or a portable binding to a new object type.

Gather due combat targets against one pre-damage world, then apply the staged
hits. A falling tree must not change another attack's target halfway through
the same impact batch. Reauthorize later observations and follow-up commands
after the mutation; do not let iteration order become hidden targeting access.

Public static geography may be distributed with the map. Hidden units, distant mutable object state and private contents cannot be published as a global registry. Static terrain and required routes remain available to authority. Streaming/LOD cannot unload an active fight, carry binding or pending transaction.

An accepted door/foliage/cargo mutation updates physical collision, sight proxy and local navigation revision atomically. A temporarily stale path stops/replans rather than crossing a blocker. Door changes must not rebuild the entire map. Simplified proxies and instanced scenery control cost while preserving agreement.

## Temporary surfaces and large-map admission

Keep the elemental reducer and item transactions shared across profiles. A larger
map needs a distinct admitted coordinate format, support-height queries and its
own bounded allocator capacities. Expanding its bounds must not silently widen
the old integration profile or reinterpret the old arena's authored substrates.

Terrain support belongs in authority state. A cell follows the local floor
slope instead of comparing every foot position with a single centre height.
A field is active at a particular floor; contact, damage and presentation use that floor consistently. Validate
all selected cells before applying a release so a height conflict or exhausted
budget cannot produce a partial paid result. Capacity rejection must release
item reservations and retain both physical instance and family readiness.

Canonical records may retain private sponsor/life/cast attribution for later
credit and conservation. Publish a separate observed surface format with only
location, state and revision. Client presentation expires independently of
server actor lifetime and never creates a second collision, navigation or
combat authority. Exercise paid RPC paths separately from diagnostic allocator
fill and transition fixtures; record each boundary in release evidence.

Portable channels reserve both the physical source and their exact allocator
budget. Keep cancel, death and supersession cleanup in the existing interaction
lifecycle. At successful release, queue the guaranteed field allocation before
charging fuel and setting cooldown endpoints. Store vegetation exhaustion on
the finite world source independently of the short-lived damage field, so
caster death or field expiry cannot recreate resources.

## Data and reproducibility

Documentation JSON is design input. Runtime consumes staged, validated payloads/native assets, never arbitrary documentation files. Record source hashes, profile, layout/fitter revision and generated outputs. Closed import contracts reject unsupported fields and semantic drift before admitting a match.

Preserve v08_integration and older labelled profiles. A fresh match selects one profile; sources, spawns, timers and coordinates cannot concatenate. Preferences may migrate; match state is not silently converted. B90's standalone spatial preview deliberately admits no full gameplay roster/economy before those stages exist.

## Verification and operations

Build Editor and Game, regenerate/validate changed assets, run relevant native rules and scenario probes, and preserve portable evidence. Native paths, movement, rendering, network privacy, ledgers, performance and human experience are distinct checks.

A packaged dedicated server depends on a compatible source engine; historical headless listen-authority workloads do not establish one. Production hosting, matchmaking, moderation, durable accounts and recovery remain later operating work. Do not infer production readiness from a local package or a four-client fixture.
