# Native multiplayer authority and private vision

## Native authority

The Unreal server owns life, damage, cooldowns, currency, contract completion, item ownership, sight permission and the Heart result. Clients request intent through their owning controller. A request naming another player, stale life, nonexistent object or hidden target must not change state.

Use Character Movement for ordinary movement, prediction and correction. Do not run the old TypeScript position integration and native character movement on the same body. Extend its supported prediction path for sprint. Integrate dash through a compatible movement source or tested custom move implementation; do not call a client-authoritative transform teleport each frame. [Epic network movement reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine).

The first dash may wait for server acceptance if necessary, provided feedback and reconciliation are explicit and tested at delay. Do not label a cosmetic dash as a legal escape before the server has moved the body. Assassination range, rear position and interruption are checked against server state, not the rendered proxy interpolation.

## Rule scheduling

Native movement follows the engine update. Project authority rules and sight refresh sample the resulting server world at a proposed 20 Hz; observed full views publish at 10 Hz. Define tick prerequisites or an explicit driver so movement is sampled consistently. Do not assume every engine subsystem follows the old web simulator's seven-step order.

Use absolute authority deadlines for commitments, protection and returns. A due action resolves at the first authority sample at or after its deadline. Unit tests use an injected clock to verify exact rule arithmetic; real-world timing tests allow one 50 ms authority interval plus a measured engine-frame allowance. Record this scheduling replacement in the ledger. It is not cross-engine lockstep parity.

In one authority batch, collect valid due combat impacts, apply simultaneous damage, process deaths and drops, commit noncombat transactions, then evaluate contest and claim completion, then the deadline. Damage on a channel-completion batch prevents that channel from completing. Newly dead actors cannot finish a delivery or claim. Recompute views after source and life changes before publication. An engine hitch must be logged; do not invent historical movement states by replaying arbitrary rule ticks against one final pose.

A late authority sample may finalize a claim whose scheduled completion was at or before the 720 second deadline, if it is still valid at evaluation. It may not award a claim scheduled after that deadline merely because both were processed in one late sample. Verify exact deadline predicates with the injected clock and report real sampling tolerance separately.

## Observation architecture

Unreal's actor relevancy is a transport decision, not the complete game's fog policy. Default distance, ownership, attachments and always-relevant settings can produce behavior that is wrong for crowd stealth. Epic documents per-connection relevance and owning-connection behavior; the following relay design is a project proposal to make its privacy contract inspectable. [Actor relevancy](https://dev.epicgames.com/documentation/unreal-engine/actor-relevancy-in-unreal-engine), [ownership reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine).

Use this first scaffold approach:

1. Canonical server bodies simulate on authority. A controlled character replicates only to its owning connection for native movement. Server residents have no replicated canonical body on remote clients. Verify the selected engine's ownership and always-relevant settings rather than assuming a flag is sufficient.
2. Each connection has one owner-only `ALDWorldViewRelay`. The server builds its permitted dynamic view from body, owned ward and public sources. The relay carries sanitized data rather than pointers to canonical actors.
3. Other bodies render as nonreplicated local `ALDObservedEntityProxy` objects driven by that view. Human, bot and resident observations use the same class and fields. The local owned body is excluded from the proxy list to avoid double rendering.
4. The client never receives an enemy canonical character, transform stream, role flag, owner ID or private statistics through another route. Audit GameState, PlayerState, RPCs, attachments, cues, debug objects and map placement as well as the relay.

Do not simultaneously build this relay, a custom Replication Graph and an Iris filtering framework. A later architecture may replace it after measured scale needs. A replacement must satisfy the same privacy tests. Rendering hidden actors black or merely disabling their mesh does not satisfy the requirement.

## View envelope and removal

An owner view includes match generation, monotonic view revision, authority time, current fog mask, exploration changes and complete currently visible entity records. A record contains an opaque observed ID, presentation key, authorized pose and facing, locomotion, permitted action cue and exposure endpoint. It has no human flag, private HP, inventory, contract, PlayerState pointer or canonical actor reference.

The proposed first transport is an owner-directed unreliable full-view RPC at 10 Hz, with reliable concealment notifications on permission loss and reliable private transaction receipts. Every full view replaces the visible set, so a lost packet recovers at the next view. Both snapshot and concealment messages carry revisions; ignore older messages so delayed snapshots cannot recreate a removed actor and delayed concealment cannot erase a later authorized reappearance.

On receipt of a newer concealment, remove affected proxies and their attached effects immediately. The server's first outgoing view after tower loss or death must exclude newly hidden poses. This is a publication rule, not a zero-latency client guarantee. A client also hides remote dynamic proxies and marks sight stale after 250 ms without a fresh authorized view; the next valid full view restores them. Test transport delay and adjust the timeout only through a recorded decision.

If the selected engine or payload size requires another transport, choose one documented alternative with explicit full-state recovery and removal semantics. Do not rely on an unspecified combination of replicated arrays, ordering and multicast events. Track payload bytes and reject pathological request sizes or rates at the authority boundary.

## Metadata and side effects

Default PlayerState data must not link a neutral body ID to a human or a private resource state. Public lobby occupancy is permitted; mapping it to a specific observed body is not. Keep actual actor network GUIDs out of observed IDs. Do not serialize object references to an unseen target in a warning or acknowledgement.

Only the owner receives exact self HP, gold, XP, contract and life timer. GameState contains public objective facts, not a list of canonical bodies with transforms. Public tower states and the Heart announcement are deliberate public facts. The fixture's developer panel does not authorize remote canonical state dumps.

Sound, trails, shadows, decals, selection outlines, hit effects and floating text must follow visibility. A hidden attack can produce the retained anonymous local bearing cue where the rules permit it, not a world-space sound that reveals its exact source. An assassination target still receives the required warning without hidden attacker coordinates.

Place no hidden gameplay character instances in a client-loaded map. Spawn dynamic bodies on authority from a role-neutral valid spawn pool using a server-only seed. Static map geometry and known POI locations may ship with the client. Do not put a fixed human-versus-resident location table in map assets and then call the initial crowd anonymous.

## Vision policy in 3D

Use planar ranges and explicit source vision levels for the first port. Low sources cannot see into or across higher vision terrain. High sources can see down, while walls and trees still block. A 3D camera that physically looks over a wall does not override server permission. A character's eye height does not automatically grant high-ground privilege; use the floor's authored vision level.

Use collision queries for opaque blockers and validated terrain or volume data for logical elevation. Trace from a consistent source sample to a target sample, excluding the source's own body. Define and test boundary behavior, ramps and trees. Fog cells are 80 cm and must not conflict with exact inclusion checks. Current sight is own living body plus owned wards plus public sensors, including distant sources. Waiting players get no body source.

The ward is a real authority object with one initial charge, 60 second lifetime, 20 HP and the retained placement and active-count rules converted to cm. Full purchase economics can remain deferred. Public sensor activation and deactivation are real sight changes; their acquisition or destruction may be a diagnostic fixture.

## Requests and transaction safety

Interaction and attack requests use observed IDs or public POI IDs. The server maps those IDs within the requesting connection's view and rechecks current legality, rather than trusting an old snapshot or accepting a raw canonical actor pointer. A visible target can still be out of melee range; remote ward sight does not create remote damage.

Include match and life generation, request ID and bounded arguments. Bind ownership to the controller connection. Rate-limit intent calls, distinguish held-state refresh from edge-triggered actions and expire stale holds after 250 ms. Do not mark a reliable RPC to run every render frame. Preserve idempotent delivery, death, pickup and result receipts across retries and possession changes.

For simultaneous Seal pickup, use the existing seeded arbitration policy. When a carrier dies, drop exactly one item at a valid ground point independently of the corpse. Returning players cannot replay old-life commands to collect again. The slice uses the real ownership transaction even when the item's initial acquisition is a fixture.

## Network evidence

Run two remote clients against a separate server world for privacy checks. Inspect serialized observations and each restricted client's actor registry, not only screenshots or a server debug view. Demonstrate that a ward helps its owner, a public source helps both clients and source loss stops fresh hidden positions. Test reappearance and delayed messages as well as first hiding.

Test a listen-host session separately for ordinary usability, and label its trust limitation. Do not claim remote-client privacy against a user who controls the authoritative host process. Packaged dedicated-server validation is conditional on the toolchain lane, as stated in setup.
