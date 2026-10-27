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

## Static and mutable world

Public static geography may be distributed with the map. Hidden units, distant mutable object state and private contents cannot be published as a global registry. Static terrain and required routes remain available to authority. Streaming/LOD cannot unload an active fight, carry binding or pending transaction.

An accepted door/foliage/cargo mutation updates physical collision, sight proxy and local navigation revision atomically. A temporarily stale path stops/replans rather than crossing a blocker. Door changes must not rebuild the entire map. Simplified proxies and instanced scenery control cost while preserving agreement.

## Data and reproducibility

Documentation JSON is design input. Runtime consumes staged, validated payloads/native assets, never arbitrary documentation files. Record source hashes, profile, layout/fitter revision and generated outputs. Closed import contracts reject unsupported fields and semantic drift before admitting a match.

Preserve v08_integration and older labelled profiles. A fresh match selects one profile; sources, spawns, timers and coordinates cannot concatenate. Preferences may migrate; match state is not silently converted. B90's standalone spatial preview deliberately admits no full gameplay roster/economy before those stages exist.

## Verification and operations

Build Editor and Game, regenerate/validate changed assets, run relevant native rules and scenario probes, and preserve portable evidence. Native paths, movement, rendering, network privacy, ledgers, performance and human experience are distinct checks.

A packaged dedicated server depends on a compatible source engine; historical headless listen-authority workloads do not establish one. Production hosting, matchmaking, moderation, durable accounts and recovery remain later operating work. Do not infer production readiness from a local package or a four-client fixture.
