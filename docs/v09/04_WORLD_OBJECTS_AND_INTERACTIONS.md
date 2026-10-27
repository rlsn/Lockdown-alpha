# Functional objects and interaction transactions

Objects share Observe/Take/Drop/Use/Carry/Place/Open/Close/Harvest/Trade where appropriate. A verb is displayed only when a real server operation is implemented. Do not add Drink to Water Flask: the inherited item supplies Wet/extinguish terrain and does not heal, restore MP or quench an invented thirst meter. Throw and Pour are alternative presentations of the same paid accessory action and share its existing range, footprint, cooldown, consumption and reaction rules.

| Type | Initial instances | Core behavior |
| --- | --- | --- |
| Tall grass | 96 | Concealment patches; finite ignitable cover |
| Sapling | 64 | Small walking/sight blocker; harvest once or clear/burn |
| Door | 72 | Open/close; resident rear locks; public front always usable |
| Common chest | 48 | Storage; 12 start with 2 Fiber + 1 Ore, 36 start empty |
| Crate | 36 | Push/carry/place, storage, finite cover |
| Barrel | 24 | Push/break/open; empty storage, no free oil spill |
| Water Flask | 24 | One physical inherited consumable instance each |
| Oil Flask | 24 | One physical inherited consumable instance each |
| Torch | 24 | Portable accessory; finite light and socket ignition |
| Lantern | 12 | Portable accessory; finite visual light |
| Rope | 12 | Portable accessory deployed at approved anchors |
| Ladder | 12 | Heavy world prop carried to an approved anchor |
| Furniture | 32 | Fixed low collision/storage interaction |
| **Total** | **480** | Exact initial manifest count; player drops do not respawn sources |

Portable items occupy the inherited 24-instance inventory and weight capacity. Torch weight2, Lantern3, Rope3; existing flasks weight1. These three new portable item definitions have Standard quality only, no stats, no passive combat bonuses, no official buy/sell source and no disassembly. Their initial source is the finite world endowment. They can be dropped/stolen/traded and drop on death even if accessory-bound. Do not reintroduce extra inventory quick-reference slots.

A portable active requires one of four physical accessory bindings; Mute blocks it. Item suppression/activation and quiet accessory swaps use v0.8 rules. Ground pickup/door/workstation/basic harvest/heavy carry are world interactions, not unbound item casts. Stun/Hex/death/protection block starting them; movement, damage, range/sight loss and stale held input cancel ordinary held interactions. Root prevents actual voluntary movement and climbing. Silence/Mute alone do not disable opening a door, but do disable learned/item actions in their inherited categories.

All ordinary world interactions use 120 cm and body sight. Defaults: pickup500 ms, door300 ms, force or basic lockpick3,000 ms, harvest4,000 ms, begin heavy carry1,000 ms, place1,500 ms. Pickup/loot/store/harvest/carry/place/force/lockpick require6 s combat quiet; a simple open/close does not. Container contents are private until legally opened/observed. Revalidate exact item/container revision, instance ownership, life, capacity, quiet and endpoint before commit. Duplicate sequence IDs return the original receipt. Failed actions change no ownership, resources or cooldowns.

## Heavy objects and traversal

A crate (weight12), barrel (16) or ladder (10) remains a world instance while carried. One heavy prop per body; capacity must fit its weight. Carry uses a carried-world binding rather than bag storage; it does not create a twelfth equipment slot or a second item instance. Both hands and all attacks/skills/items/services are suppressed, movement70% baseline. Damage/death or an explicit ability request drops it safely before any subsequent legal action; a stunned character cannot exploit that to cast. If placement fails, it stays carried; forced drop searches a deterministic clear pad and never deletes it.

Push is a short authored 150 cm translation over 1 s using a swept capsule-sized proxy, same weight requirement and quiet check, no velocity simulation. Stop at static collision; never push another character or cause falling damage. Carry is not required for barrels. Placement never stacks and must preserve the reserved corridors/clearances in03. Breaking crates/barrels removes that proxy and drops their actual stored contents once; it creates no material reward or payload that was absent from their ledger.

Ladders deploy only to the 16 anchors. Ropes deploy only from a bound accessory, retain their instance ID as a world-installed object, clear their binding atomically and do not remain in inventory. Ladder placement is a carried-prop world interaction. Both create a labeled bidirectional 2 s traversal link with ≤5 m rise. One user at a time; reservation timeout3 s, ordinary body fitting and Root/control checks. Owner can retrieve/detach only while unused and after6 s quiet, with full capacity validation. Everyone may use an installed link, including rivals. Stun/death during traversal returns to the last validated endpoint, without falling damage or clipping. Permanent stairs always remain.

## Torch, lantern and doors

Torch accessory has180 s total lit fuel, retained through swaps/drop/death; Lantern300 s. No free refill, vendor refresh, relight reset or copy through deployment. Toggle/ignite/extinguish is an item action. Cosmetic illumination is6/8 m and grants **zero** extra sight authorization, detection, wall vision or minimap scouting. A lit dropped light can show a cue only to observers who could already observe its location. Darkness changes appearance/readability, not target permission in v0.9.

Torch socket ignition is a1 s item channel, cooldown12 s, up to four eligible2 m cells, Burning3 s, normal surface DPS/attribution and field reservation. It consumes fuel, not the item. It cannot directly damage/stun a unit or automatically ignite every object passed while walking. Mute/quiet/normal targeting still apply. A dropped torch creates light only, with no autonomous burning field. Continuous fuel decreases only while lit; ignition requires at least1 s remaining fuel and accepted field capacity. Empty fuel disables lighting/ignition.

Doors start unlocked. A resident owns the north/rear lock; contenders can pick/force it but cannot claim free lock ownership. Basic lockpick is a quiet3 s interaction without a new spell. A chosen future skill bonus requires an explicitly cataloged action; no generic DEX roll is inferred. Public south doors cannot lock. Closing rejects if a capsule intersects the doorway. A forced rear door stays broken-open for the match. Service-body protection does not transfer to the burglar, building, cargo or nearby street.

Object health beyond saplings is simple deterministic break channels for the demo, rather than a second damage resistance catalog: quiet3 s break on crates/barrels, canceled by damage/movement. Fixed furniture and shell walls are indestructible. Implement the defined verbs, not a generic break-anything action.

