# Unreal integration, authority and UI

No repository, delivered build, engine version or actual performance profile was supplied. Begin by recording source commit, engine version, build/run/test commands, current map framework, player movement/capsule, terrain/navmesh, native fog, interaction/input, combat/GAS usage if present, inventories/economy, NPC scheduling and widgets. Reuse the architecture. This document specifies contracts, not version-specific Unreal APIs or a requirement to migrate to GAS/World Partition/PCG.

Recommended boundaries: MapDefinition importer/builder; persistent WorldObject registry; authoritative interaction dispatcher; local collision/vision/navigation mutation service; NPC schedule/intent service; existing contender action/economy/route services; telemetry collector. DataAssets/DataTables or an existing validated JSON pipeline are acceptable. Functional actors use stable IDs; decorative instancing may use coarse proxy IDs. Unknown type, function, source, action, legacy alias or unit is an import error.

## Data import and overlay

1. Read the preserved v0.8 archive's catalog/data without importing its profile/spawns/economy as a second active set.
2. Apply `data/rules.json`, `economy.json`, manifests, `world_objects_rules.json`, population and route data for selected v09_full. Preserve unmodified inherited combat/item definitions.
3. Adapt legacy nodes through `legacy_aliases.json`; this is routing compatibility, never duplicate actors/stores. Add the three new portable definitions from04.
4. Bind object verbs, service functions and source receipts to actual server implementations. Validate everything before spawning any playable match.
5. Record input hashes, profile, seed, fitted layout revision and source commit in a build manifest. Refuse mismatched clients/maps, centimeter/meter conversions or partial import.

JSON schema_version9 denotes the map overlay family; it does not rewrite the embedded schema8 catalogs. The importer explicitly adapts those catalogs. All coordinate distances are integer cm, times integer server ms unless a field ends `_s`, gold/counts integers. Route length is a planar polyline estimate. Canonical stage IDs are B90–B94; feature/test IDs extend those prefixes.

## Interaction protocol

WorldInteractionRequest includes matchId/profile/layoutRevision, contenderId/lifeId, sequenceId, objectId/objectRevision, verbId, optional exact item IDs and placement anchor/point, inventoryRevision, held-input nonce. Server derives action category and validates observation, range, life, protection, control/quiet, item binding/cooldown/fuel, owner permissions, reservations and capacity. Client flags never relabel an accessory action as a world action.

Hold records store start/end, reserved source/item/anchor revisions and owner/life. Reservation ownership is not a second item location or immunity to mandatory death drops. Completion revalidates before one atomic mutation/reward receipt. Deduplicate by accepted sequence and source receipt IDs. If two bodies take the same ladder/flask/Crystal charge, earliest accepted valid commit wins; the other loses nothing. Cancelled channels release reservations. A mutation publishes new physical proxy, sight proxy, local navigation revision and authorized visual state in the same authoritative batch.

Replication sends observed world state and explicitly public institution/objective messages. Static landmark/road geometry can be on a public minimap; hidden dropped loot, fogged door/tree changes, contender intention, crime/last-seen identity and NPC role flags cannot leak via full registry snapshots. On re-observation send the latest authorized revision, not an unconditional live event stream. Public ledger/admin tools stay outside ordinary payloads.

## Streaming and navigation

At this footprint, chunk terrain/scenery and use hierarchical region/route planning plus local native right-click navigation. Always keep coarse river/terrain/critical-route collision available to authoritative movement. Server streaming cannot unload an active fight, carried prop or transaction. Client unloading a visual cell cannot change server sight or access. Origin shifts, if the audited project uses them, transform queries consistently but preserve logical world cell IDs.

Do not rebuild the whole world navmesh when opening one door or moving a crate. Mark local dirty tiles/obstacles and maintain a synchronous collision stop while local paths catch up. A stale route may stop/replan, never pass through an accepted blocker. Essential route envelope is protected from cargo. Repeated right clicks across frozen rivers, roofs, doors and return paths must finish or report a truthful blocked reason without an infinite repath loop.

## Player UI

Retain right-click move/auto-navigation, A attack, S stop, F context hold, T trade, six actions Q/W/E/R/D/G and four accessories1–4 with remapping. Hover shows object name, available verb, owner restriction and held-time preview only when authorized. The F selector resolves a crowded location without stealing a combat click. Carrying shows the object, speed70%, suppressed actions and Place/Drop; invalid placement gives a clear generic reason. Show ladder/rope endpoints before commit.

Minimap distinguishes cities/towns, discovered/public institutions, static roads/bridges/fords, local authorized unit sight and intentionally public Heart warnings. No live key-carrier icon. Optional route preview shows estimated distance/time and known dry/Wet choices without hidden hazards or opponents. Selected-unit inspection still uses authorized Unknown/private fields; clicking an NPC does not expose competitor status, class or inventory. Inventory/trade uses existing atomic transaction UI, no separate duplicate loot copy.

Implementation report must record real native evidence for render/collision agreement, restricted-client payloads, remote-client mutation updates, interaction races, save/rejoin/source identity, whole-map paths, and service economy ledgers. Screenshots alone cannot establish authority or no-duplication.

