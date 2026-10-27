# Vision, navigation and bounded environment

Use server-owned visibility authorization across rendering, target handles, minimap, selection, sound/VFX messages and debug serialization. A hidden unit cannot be inspected just because it stands in a loaded tile. Public static map geography may be known; unit positions, private item contents and ongoing distant mutations are not globally disclosed unless an explicit public rule authorizes them.

Own sight is 18 m; a public watch source is 24 m. High ground uses a deterministic one-way ridge rule: a lower-band sight source cannot see a target across an intervening tagged upward ledge edge; a source on the higher band may see downslope within its normal radius if no wall/tree/smoke blocks the ray. Same-band rays follow normal blockers. A ramp itself has no artificial opaque wall; fit its side ledge tags so visibility changes are readable. Height grants no automatic range multiplier, damage bonus, miss RNG or ability to see through walls. Public watch sources reveal local bodies to all contenders through the same geometry, not entire city districts.

Sight uses simplified proxies consistent with native movement geometry: shell walls, doors, tagged trees, cargo cover and ledges. Not every small prop occludes. Tall grass is concealment within already valid geometry, not a solid sight wall. Ordinary observers within 2 m reveal grass occupants if they have unobstructed geometry; authorized detectors can also reveal concealment. Leaving grass, attacking or casting removes its concealment; offense reveals for 2 s and concealment rearms after 1 s without offense once that reveal endpoint has passed. Those timers do not grant identity or private stats. Burning grass loses concealment and stays burnt for the match.

Grass cannot conceal an active Heart claim. Oil/fire/steam work with the same six conditions, finite Oil overlay, geometry masks and per-source receipts as v0.8. Neither Nature Overgrown nor arbitrary VFX creates foliage concealment. Smoke/steam remains symmetric union-boundary occlusion; detection never penetrates it. When a cloud clears or a tree falls, recalculate authorization before accepting a unit-target follow-up.

## Traversal invariants

All river floors are walkable, with no baseline river slow, swimming, drowning or mandatory bridge. Contact immediately applies Wet before same-boundary hits and refreshes every 100 ms; residual Wet lasts 3,000 ms unless an inherited item explicitly alters it. Ice still refreshes Wet, slows 30%, and never changes connectivity, adds sliding, or stuns by contact.

Only a qualifying direct Frost hit on a Wet ordinary victim attempts the inherited 600 ms stun and sets the 6,000 ms victim-wide reaction lock. The lock survives death/rejoin; Control Guard may prevent Stun while consuming the attempt. Frost Capsule, frozen floor and periodic surface ticks do not qualify. Keep all v0.8 damage/protection/attribution/expiry ordering and receipt rules.

Permanent bridges cannot be destroyed in this demo. Their deck stays dry above the river-contact volume; lower river floor remains walkable. Optional scenery planks can break only if they are outside required routes and not falsely represented as these bridges. Roads crossing rivers away from named decks are low-bank Wet fords, enumerated in routes.json. Two crossings of the same river do not share dynamic electrical/fire propagation.

Buildings and permanent scenery trunks are fixed walking blockers. Saplings, doors and approved moved cargo can change local collision. Every accepted mutation updates collision, simplified sight proxy and local navigation obstacle revision as one server transaction. Dirty local nav regions may rebuild incrementally or use native avoidance/obstacle layers after auditing the existing project; the contract is consistent behavior, not a required Unreal API.

Preserve a permanent connected route graph under every optional prop configuration. Reject cargo placement in reserved corridors, within doors/stairs/spawn/Heart/service reach pads, on another body or on another prop. Minimum local walk clearance is 3 m. Never rely on a hidden opponent's coordinates to explain a failed placement; return generic placement feedback. No stacks, dams, free walls, bridge construction or world-bounds launching.

## Mutable foliage extension

Only authored `sapling` and `tall_grass` instances can be ignited by v0.9 socket interactions. A sapling has 80 HP, 4 s basic harvest/chop, and a single finite Fiber output if harvested; damage/chopping for clearance or burning produces no Fiber. Fire marks a socket once, uses bounded Burning cells, and removes its sapling proxy after 3 s. The cleared state persists as a map mutation, not a permanent damaging field. At expiry the fire stops. The original object/source ID cannot yield a second harvest after death, reload or a second attacker.

No fire spreads to adjacent trees automatically. No recursive reaction output processing. Terrain mutations cannot exceed the source instances authored at reset. Owner cell64, match cell1,024 and transition2,048/100 ms caps apply in v09_full; all other 2 m cell selection, reservations, reject-without-spend and lifetime limits remain v0.8. Those expanded match capacities require B94 stress evidence.

