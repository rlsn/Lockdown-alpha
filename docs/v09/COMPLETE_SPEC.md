# Project Lockdown v0.9 — Full Map Demo Specification

27 October 2026 · revision 1 · stage IDs B90–B94

See atlas/ for world and city detail PNG/SVG maps.

# Decisions, precedence and profiles

The user accepted the recommended world scale, hybrid geography, and selected interiors from the map proposal, then required B90, B91, … stage IDs. Those choices are normative. Other new numbers in this package are concrete first-demo defaults for implementation and tuning.

Precedence: current user decisions → v0.9 normative prose and active data → unchanged v0.8 rules/catalogs → v0.7/v0.6 inherited semantics through the supplied archive. A disagreement between prose, data, imported assets or server behavior blocks the affected release gate; record and reconcile it, never pick whichever value is convenient.

| Topic | v0.8 integration | v0.9 full profile |
| --- | --- | --- |
| Deadline | 2,100 seconds | 5,400 seconds; separate 3,600/4,500 tests |
| Contenders | 1–4 | Exactly 24, 1–12 human seats, remainder ordinary contender AI |
| Geography | City, WestTown, EastTown, small wilds | Capital + four cities + six towns + connected wilderness |
| Buildings | Baseline-specific | 144 shells; 36 enterable; 16 roof anchors |
| Camps | 3 × 12 spawn identities, 150 s minimum gap | 12 × 6, 600 s minimum gap; at most one live per camp |
| Other rare sources | 3 ruins, 3 towers, 1 boss | Same unique quantities, relocated |
| Courier budget | 8 paid jobs per contender | 20 paid jobs per contender, same 25 gold/45 XP |
| Markets | 3 × 150 gold | 5 × 600 gold, explicit finite initial stocks |
| Dynamic field cap | 256 cells / 512 transitions per 100 ms | 1,024 cells / 2,048 transitions; still 64 cells per owner |
| Vegetation mutation | Flammable sockets, unchanged scenery trees | Only listed grass/sapling instances can lose concealment/collision/vision proxies |
| Other combat | Six shared actions + four accessories, one class trait | Retained, including inventory, attributes, spell ranks and counters |
| Victory | Four routes to one 10 s physical claim | Retained; Charter activation moved near Heart to fit warning |

The v08_integration profile remains an isolated regression profile using the original data, geography, limits and accounting. Never concatenate its spawn pools, initial markets or rewards with v09_full. Start a fresh match to change profile. No hot conversion of inventories, objectives, timers or map coordinates. Account UI preferences may migrate; match progression does not persist between matches.

Do not scale combat range, capsule size or character art to fill the 2.4 km map. Movement remains **420 cm/s**, not the proposal's approximate 5 m/s. Base own-body sight is explicitly selected as **1,800 cm** for the full demo; public watch sight is **2,400 cm**, both clipped by geometry. The inherited +50 cm own-sight gear cap remains. These new visibility defaults need native camera/readability tests; they are not retroactive claims about the existing game.

All players and contender AI retain level cap 15, 53 starting natural points, natural 8–16, 24 carried instances, seven body bindings, four physical accessory bindings, six shared active slots, one item capstone, and one intrinsic class trait. No new starting money or equipment is granted here. Audit actual starter kits against v0.8 before implementation; do not silently strip delivered items or fill empty slots.

Full-session progression is not an invitation to multiply every reward by elapsed minutes. Prices, rank gates, tower/boss combat tuning, Charter fee, 30/60-second route windows and the 10-second claim remain unchanged except the explicit overrides above. Camp timing, courier count, initial market resources, source endowments, spatial discovery opportunities and field capacity are the enumerated full-map changes. Human pacing remains a test result.



---

# Spatial layout and travel

World bounds are X/Y −120,000…+120,000 cm. The outer 2,000 cm is a decorative safety apron, giving an ordinary-body center limit of ±118,000 cm; playable land connects inside this apron. World origin is the center, not a settlement. The 200 cm environmental grid begins at (−120,000, −120,000, 0). A cell uses floor((position−origin)/200); no changing grid origin when streaming.

| Settlement | Center (meters E, N, height) | Footprint radius | Role |
| --- | --- | --- | --- |
| Crownreach | 40, 40, 4 | 200 m | Capital, institutions, final contest |
| Highfort | −820, 720, 8 | 125 m | Garrison, cliff paths, fortification |
| Lumen Archive | 780, 780, 4 | 125 m | Archive, gardens, spellbooks |
| Riverport | −800, −660, 0 | 125 m | Port, exchange, warehouses |
| Ironworks | 760, −680, 4 | 125 m | Ore, workshops, advanced forge |
| Oakmere | −770, 80, 0 | 60 m | Forest-side work/rest hub |
| Millford | −390, 450, 0 | 60 m | Northern river trade junction |
| Reedbank | −470, −650, 0 | 60 m | Marsh and port connector |
| Ashcross | 380, −850, 4 | 60 m | Southern logistics junction |
| Bellwick | 730, 200, 4 | 60 m | Eastern tributary crossroads |
| Stonewake | 0, 840, 8 | 60 m | Ridge crossing and northern access |

These are spatial centers, not piles of rewards. Six separate service/neighborhood POIs sit in each city. Urban districts mix watched service fronts, public streets, private back doors, cargo pads and roof positions. Towns provide workshop/preparation/recovery/work-board interactions without spawning new regional markets or rare-stock pools.

The main river meanders north–south west of Crownreach. An eastern tributary passes north of Crownreach and joins the main river. A walkable port canal connects Riverport’s southern quay to the main river; it stays south of the building blocks and creates a real waterfront. Named bridges are North, Mill, South and Garden; named fords are Crown, Reed and Bell. Other trail/river intersections are ordinary low-bank walkable fords. `routes.json` lists each intersection and whether it uses a named dry bridge. A line drawn as a road is not automatically dry. Banks are ramps, never hidden swimming/climbing gates.

Three macro elevation bands (0, 4, 8 m) create marsh/lowland, built terraces and ridge positions. Local noise is capped at ±1.5 m outside reserved pads. City/POI/road pads and connections override noise. A plateau edge is not a teleporting discontinuity: the builder must carve continuous ≤25% ramps or stairs on every ordinary route; side ledges use collision and optional traversal anchors. No route can depend on owning a ladder.

The 96-POI catalog contains 11 settlement hubs, 30 city service POIs, 12 camps, three ruins, one boss, one Heart forge, three towers, one Heart, one antechamber, 12 crossings/passes, nine resource deposits and 12 shrines/landmarks. Some settlement hubs aggregate services, so 96 is a count of addressable locations, not 96 unique reward pools. Every location has at least two functions in the manifest. Decorative houses are not extra POIs.

## Travel and density targets

Use the actual 420 cm/s baseline. A 100 m unobstructed walk takes 23.81 s; 600 m takes 142.86 s; 1,000 m takes 238.10 s. The 2.4 km side takes 9.52 min and the theoretical corner diagonal takes 13.47 min before detours. Neighboring outer cities can require roughly 6–8 minutes on the graph; outer city to a nearby town should be nearer 2–4 minutes. The original 3–5-minute city estimate is not a guarantee at the inherited speed.

`atlas/TRAVEL_TABLE.csv` contains shortest strategic graph distances and nominal times. Add native path length, walking time, stops and hazards as distinct recorded columns during B90; do not call graph estimates measurements. Record outer-city→capital, every city→its nearby towns, all spawn→core service, ruin→antechamber, forge→antechamber, and antechamber→Heart. `nominal_travel_s` ignores slopes, animation, crowd waits, carry speed, slows and obstruction detours.

Target a recognizable major choice every 45–90 seconds outside settlements and smaller useful interactions every 10–25 seconds along tested routes. These are acceptance targets, not guaranteed by dividing map area by POI count. Plot walked empty segments and move existing props/landmarks into gaps before increasing the footprint. No activity XP is paid simply for walking through those intervals.

The perimeter road ring connects all four outer cities through towns; central spokes and trails cross-connect it. Rare targets have two named graph approaches, with an occluded or alternate branch. There is a dry southern Heart flank as well as a wetter western approach. The Heart model is not a collision barrier for defenders. Ladder, private doors, scenery mutation and faction control cannot eliminate ordinary access to institutions, ruins or the Heart.

All authored roads use 8 m width; trails/district lanes 4 m, before local fitted door/capsule clearances. The world must remain navigable at zero movement upgrades. Transport, mounts and free recall are not required. Gate/recall restrictions on objective cargo remain v0.8 rules; do not bypass long physical objective transport with a map UI button.



---

# Deterministic world generation and buildings

Seed 9001 and layout revision 1 produce one reviewed map. Major settlement centers, roads, river centerlines, victory objects, rare sources and selected interiors are fixed across normal matches. PCG-style detail means seeded placement within constraints; it does not mean shuffled victory geography each game. A separate future map revision may change it. Match reset restores the chosen revision without changing the seed.

Generation order is terrain bands → land flattening and connected ramps → river floor/banks → roads/bridges → settlement blocks → POI pads → authored visibility/nav proxies → functional props and traversal anchors → decorative vegetation → institutions/sources → NPC anchors. Reserve critical corridors before scattering detail. Deterministic streams use independent layer seeds so adding a flower does not move a ruin, a building or a prop ID.

`tools/rebuild_layout.py` emits semantic placements and route geometry. It is not a Landscape generator or an editor plugin. B90 must add a native builder that imports centimeters directly, uses the existing project terrain architecture, snaps pads once, preserves IDs and serializes final fitted transforms. Final terrain must honor the manifest's height at each gameplay pad, with connecting ramps. A signed-off fitted manifest may override a generated transform only with the same ID and a documented layout revision.

The 12 forest patches target 250 decorative trees each (3,000 total), plus cheap noninteractive ground cover. These decorative trees have stable coarse trunk/vision proxies where appropriate, but cannot be harvested, burned, removed or converted into material drops. The separate 64 saplings and 96 grass patches are the mutable gameplay foliage. Never give every decorative instance a ticking replicated actor.

## Buildings

Each city has 24 buildings; each town has four: 5×24 + 6×4 = 144. Exactly six per city and one per town are enterable: 36, or 25%. Base shells are 14×18 m; the capital spreads them farther apart. Six city interiors host the named district services; a town interior hosts basic work/recovery. Buildings can include ordinary residents and storage even when a service shares their ground floor. An official body's protection is not a building-wide protection aura.

Each enterable shell has south and north doors, 72 door instances in total. South doors are public and cannot be locked. North doors lead to a resident-controlled rear entrance and may be locked; forcing/lockpicking or the public front remains possible. Doors start closed/unlocked. Sixteen selected buildings have an upper floor/roof anchor; permanent internal stairs provide access without portable gear. No invisible second-floor room exists merely because the shell is tall.

Interiors use the same world space, authority and visibility system. Walls block walking and sight; windows are explicitly authored openings in both physical and sight proxies. Small windows may allow sight without fitting a capsule. A closed door blocks its doorway's sight and walking; opening it updates both in one revision. Roof fading changes rendering only. Occluder walls continue to work while visually faded.

Interior furniture is sparse enough for right-click navigation. Storage is a real inventory container with capacity/ownership, not an infinite source. A closed shell remains a useful exterior occluder, but no cursor suggests a nonfunctional entrance. Camera cutaways must not reveal hidden actors/items through ceilings. Inspecting a location never grants x-ray vision.

## Placement constraints

Semantic prop centers avoid building footprints and keep 2.5 m between non-door instances in the generated data. The native fitter must also test capsule sweeps, real mesh size, terrain grade, rivers, doors, stairs and the reserved 4 m route envelope. It may search a deterministic 10 m radius around the proposed point, then reject the build with the ID if no legal pad exists. It may not silently omit an interaction or silently scatter it into another region.

Procedural trees must not occupy a functional prop pad, a service reach point, a traversal endpoint, a bridge, the Heart circle, a spawn pad, or an approved road/trail envelope. Scenery that looks like an impassable wall must have a matching server proxy. No camera-only tree gaps, navmesh-only doorways or collisionless visual cliff walls.

Generated `props.json` is the exact initial count of functional object actors/instances, not the count of all scenery. Traversal anchors are sockets rather than additional portable ladders. Three rare ruin cache actors use reward source definitions separately; the 48 common chest objects contain no rare objectives. There is no extra copy of a key inside a visually similar chest.


The Riverport quay is the smuggler_cove landmark at (−800,−805,0 m), connected to the port canal. Place fixed non-navigating dock/boat scenery here; it does not provide free transport, inventory storage or a new market pool. All canal floors and banks follow the same permanent walkable-water contract.


---

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



---

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



---

# NPC population, routines and local security

Default: 96 civilians +32 travelers +24 guards +20 marked service officials +24 hostile bodies +20 contender AI =216 NPCs, plus four humans =220 total bodies. The 24 contenders include the four humans; a human replaces an AI seat. With12 humans, there are12 contender AI and208 NPCs total. Spawn pools are capacities, not mandatory fully awake brains in every camera view.

The 24-hostile live cap is12 camp creatures +one boss +11 ambient wildlife/bandits. Ambient hostiles pay no gold/XP/items. The camps have72 finite identities over the match; dead bodies do not instantly respawn. Civilian/guard/official/traveler kills never mint rewards. Contender-vs-contender XP remains50 per eligible kill,300 per contender maximum and180 s pair cooldown; zero PvP gold mint. The protected official bodies cannot be harmed. Other noncontenders may flee/be harmed under ordinary rules, with no farm reward or magically generated purse.

`npc_schedules.json` gives152 concrete civilian/traveler/guard assignments. Civilians use a600 s work/market/rest/return loop with staggered phase offsets; durations are desired activities, not mandatory teleport deadlines. Travel takes real time and can overrun a phase. Nonenterable homes use exterior doorstep anchors; no NPC walks through a sealed shell. Travelers dwell45 s, then use the same route graph between named settlements and back. Their cargo is **visual sealed logistics**, not interceptable market stock, an item source, or a second wallet. Physical supply-chain caravans are deferred. Real player courier contracts remain physical, transactional gameplay.

Official services are marked fixed institutions. Twenty official bodies can staff/share nearby counters; all36 service POIs need not have one humanoid cashier each. A marked workstation/kiosk is a valid service object. Stable official prices and dynamic market quotes remain distinct. No dialogue tree, LLM response, hidden negotiation or human-vs-bot label is needed. Action panels and world cues communicate service availability.

## Guard model

Security is local and observed. A guard directly seeing an unlawful attack or forced private door through normal sight geometry records suspect contender/body ID, last-seen point and a120 s incident expiry; no map-wide hidden tracker. Hitting an ordinary guard/resident is unlawful; ordinary hostile wildlife and contenders fighting outside marked civic zones are not automatically crimes. Civic zones are settlement central disks: capital120 m, city70 m, town30 m, excluding the always-contested Heart precinct disk (radius70 m around Heart). The Heart zone overrides civic law, so guards cannot become an unanswerable defense against a valid claimant.

Witnessed incidents broadcast an alarm to same-settlement guards within30 m, carrying last-seen facts only. Guards investigate or pursue for at most60 m from home, lose target after5 s without sight, search last seen for5 s, then return. An unobserved crime does not create an incident. No persistent criminal record, prison simulation, bounty gold, automatic fine or omniscient theft detector is added.

Guards are damageable nonreward bodies with600 HP, armor10, MR15%, basic physical attack35 every1.5 s, range220 cm, move420 cm/s, sight1,800 cm; no hidden stun/root. No gold/loot drop. A defeated guard stays down90 s, then returns at its home using the same persistent source ID, without new money or reward. Safe reentry requires a clear pad and standard3 s protection, during which it cannot attack. Maximum24 guard identities, never escalating infinite reinforcements. Civilians flee toward a known local guard/post and resume their work after10 s without observed danger; they do not join a contender army.

Rear-door lock ownership and held-container access provide a small theft/infiltration demo. Theft is only a real transfer from observed accessible inventory; sealed cosmetic traveler cargo is not loot. No arbitrary pickpocket chance/minigame or inventory inspection API is introduced by map scope. Security should create watched-core versus vulnerable-edge decisions without suppressing the final contest or turning the whole population hostile.

## Contender AI

AI contenders use native action/commerce/crafting/training/route commands and the same resources, slots, visibility, gates, death drops and cooldowns. `contender_ai.json` defines macro states and weighted goals. Establish resources → pursue a feasible route → respond to public warning if its own travel is feasible → contest physically. Keys, Fragment and capstones must be acquired legally, including from observed world drops/trades. No remote knowledge of dropped keys, hidden HP, human label, wallet or fogged hazards.

B92 requires working courier/supply, local combat/progression and reaction to observed hazards. B93 adds genuine four-route decisions and interruption. If one route policy is not implemented, mark it Not implemented; do not spawn route-complete bots and call them autonomous. Fixed budgets lock to N=24 at match start even when a human disconnects or an AI seat changes control. A takeover preserves one contender ID, inventory and counters; no replacement endowment.

## Simulation tiers

Near radius100 m: perception5 Hz, macro/local decisions2 Hz; mid100–300 m: decisions1 Hz; far schedule0.2 Hz. All player-observable movement/combat, projectiles, claim timers, inventories and institutions remain on authoritative native event time. Any observer, combat/hazard, public objective, pursuit or pending transaction wakes the body. LOD may reduce planning/animation/replication frequency, never duplicate bodies, teleport through observed space, skip lethal hazards or reset a receipt.

Record near/mid/far counts, path-query cost, perception cost, environment transitions and replication relevance. A220-body static manifest is not a performance measurement. Restrict replication before serialization; do not send full distant actors/inventories and merely hide them in the renderer.



---

# Full-profile economy, sources and progression

Only `v09_full` uses these overrides. `data/economy.json` is the full-profile commodity/contract config derived from the v0.6 contract inside v0.8. v0.8 combat/item/acquisition data remain inherited; there is no separately active legacy economy timer alongside it. Ignore older contest-by-proximity/damage wording in v0.6: v0.8 claim semantics govern.

Five regional market pools each start with600 gold, explicit stock/base-price rows and the inherited stock-dependent unit quote formula. Those are3,000 spendable gold, not five infinite vendors. The96 civilians each start with3 gold (288 total); travelers/guards/officials start with no spendable wallet. Contender money/starter kits require baseline audit; the package's selected incremental contender grant is0. Normal-profile initial accounted gold is3,288 under a zero-wallet baseline; if actual authorized starters differ, add that amount to the initial ledger and regenerate the bound before release.

Markets are Crownreach Treasury, Highfort Exchange, Lumen Garden Exchange, Riverport Harbor Exchange and Ironworks Foundry Exchange. Capital uses a treasury counter as its market rather than adding an unlisted31st city service POI. Every official supply endpoint shares global rare-stock limits per definition: visiting four shops does not create four Circle-III stock pools. Circle-I books40, Circle-II60, Circle-III90 with two copies per definition globally; Circle-IV remains assigned unique world sources. Catalog80 items/52 actives is inherited, not regenerated with renamed IDs.

World endowments are explicit sources, not extra reward gold:24 Water Flasks,24 Oil Flasks,24 Torches,12 Lanterns,12 Ropes,12 world Ladders;12 common caches together hold24 Fiber and12 Ore;64 saplings can each yield one Fiber by harvest; nine deposits have six one-unit charges each (five Crystal, two Ore, two Fiber). They do not replenish. Initial market stocks are also explicit endowments. No automatic commodity buyback for gear, books, lights, rope or ladders. Selling gathered commodities transfers a market's finite cash, never mints gold.

## Sources and sinks

| Gold source | Full-session maximum |
| --- | --- |
| Courier:20 ×25 per contender | 500 N |
| Core procurement:3 ×80 per contender | 240 N |
| Supply:1 ×50 per contender | 50 N |
| One global shortage order | 40 |
| Camps:72 ×12 | 864 |
| Towers:3 ×40 | 120 |
| Boss:1 ×90 | 90 |
| **Total reward mint** | **790 N +1,114 =20,074 at N=24** |

Initial gold3,288 +mint upper bound20,074 =23,362 before burns, under the stated starting-wallet assumption. This is an upper bound, not a forecast or entitlement. If mobs/jobs are unfinished, much less enters circulation. Shared global procurement limits remain3N Core orders andN Supply orders, with per-contender caps. No appearance of a new civilian, respawn, loot corpse, empty chest or simulation wake mints money.

Official purchases and recipes/quality fees burn; peer and market payments transfer; bank deposit fee ceil2% burns; death burns ceil15% carried wallet; Charter fee100 burns. Producers spend from real market reserves into an abstract nonspendable sink. At t=180,360,…,5,220 s, Riverport may create one Fiber for2 gold and Ironworks one Ore for3, then consume one of their own commodity and one Capital Toolkit.29 scheduled opportunities give at most145 producer burn. Skip if insufficient cash/stock/capacity, no catch-up or hidden deficit funding. Consumption destroys actual items and pays no gold. The one Toolkit shortage window is1,800–1,980 s, global bonus order once, not once per city.

`goldEnd = goldStart + rewardMints − burns`, summed across wallets, banks and all five market reserves. For each definition: `itemEnd = itemStart + created − consumed − explicitCleanup`, summed across carried/equipped, world, containers, markets and installed rope. A carried-world prop and installed item occupy one location, not two. State changes preserve instance ID; rare drops/activation/crafting retain lineage. End-match cleanup is separate from in-match economic sinks.

## Work and profession path

Courier boards issue bounded contracts with distinct reachable endpoints250–800 m apart by strategic route distance,25 gold/45 XP,20 paid jobs per contender. The server reserves a unique sealed contract token; it is not ordinary sellable cargo and cannot be duplicated, discarded for gold or counted as a book. One active courier job per contender. Death drops the token; the same owner may recover and finish, or abandon with no payout/no replacement of consumed paid quota. Quota is consumed on successful delivery, not on acceptance; abandoned contracts use a separate bounded issuance counter of40 per contender to prevent spam. Rivals can move/drop the token but cannot redeem another owner's contract. New job issuance needs no hidden opponent facts.

Industrial and Supply endorsements remain distinct real inputs:one Core→80 gold/60 XP, max3;two Toolkits→50 gold/30 XP, once. All city work counters share these budgets. Basic Toolkit/Core recipes and6 s quiet channels remain, with no passive trade/craft XP. Specialist skill shortcuts require learned/equipped slots and actual gates. Bank counters share one per-contender account across five cities; physical range/quiet/deposit fee rules remain. Only Crownreach Treasury sells the registered Charter.

Witness remains the existing audited Heave/Parley/profession route from the baseline, with no free world-interaction substitution. The supplied inherited docs describe alternatives but do not fully specify the old score/cooldown implementation. B92 must locate and record it in the actual repository; if absent, implement an explicit agreed contract rather than silently grant Witness. This is a flagged baseline dependency, not authorization to skip the zero-kill route test.

Full-profile first settlement visits grant25 XP once per distinct settlement ID, up to11=275 per contender; ruins grant50 discovery XP each once, never per life. Camps72×90, towers3×150 and boss250 remain finite world combat XP; PvP has the inherited per-contender cap. No XP from repeated surface reactions, guard/wildlife kills, harvesting, cargo pickup, market turnover or ladder use. Reaching level15 is not guaranteed to everyone; record actual distributions. The map supports growth through work and combat without requiring all civilians to fight.

The inherited zero-kill Charter arithmetic (three couriers75 −Toolkit inputs36 +Supply50 −Core inputs54 +procurement80 −Charter100 =15) remains an illustrative price-state check. Actual full-profile quotes, Witness costs, travel, purchased defenses and competition must be measured. Do not claim the15 spare gold funds a full combat build. Additional courier jobs are legal options, not free money on match start.



---

# Victory geography and match pacing

All four routes authorize the same physical Grand Heart interaction at(40 m,40 m,4 m). Claim radius120 cm, hold10 s, one winner; proximity alone never blocks. The Heart's protection is a permission check on claiming, not a wall keeping defenders outside. The final precinct remains accessible without a route, mandatory elemental ability, destroyed bridge or ladder.

| Route | Required real progression | Activation / geography |
| --- | --- | --- |
| Siege | Destroy three vulnerable Grand Towers | Global permanent access after third; each loses its own local public watch |
| Charter | Witness +Supply/Industrial endorsements +100 gold registered purchase | Purchase at Crownreach Treasury;4 s activation at antechamber;30 s warning then60 s open for everyone |
| Keys | Carry all three distinct keys; no consumption |4 s antechamber activation;30/60 owner lease;90 s owner+instance reuse locks |
| Heartbreaker |2 Core +one catalyst +one boss Fragment +20 fee;8 s vulnerable forge craft |6 s antechamber activation consumes artifact;30/60 same-life owner lease |

Only Charter activation moves from its distant civic source to the common antechamber(40,−5,4 m). This resolves a spatial response problem without inflating every warning. Purchase stays at Treasury. Nominal antechamber→Heart45 m is10.71 s, leaving time to move after a30 s warning if not interrupted. Measure the real path; target ≤15 s normal walking. Actual warning publication still gives no fogged unit position or private inventory.

Three towers stand at(−90,190,4),(195,10,4),(10,−155,4) meters. Each uses inherited1,800 HP,20 armor,25% MR,80 physical/1.5 s,6.5 m attack range and24 m public watch clipped by geometry. Their sight does not cover hundreds of meters to a city. No outer-tower prerequisite is added. Each gives40 gold/150 XP once and one contestable Tower Relic; last hit is not invulnerable ownership. Optional shortcuts may open after destruction but ordinary routes already exist.

Unique key/catalyst/book bundles are Glass Monastery northwest, Drowned Observatory far south, Ash Vault southeast. Keys are one each, not one per contender; cache outputs use actual unique instance ownership. The single Guardian at Hollow Crown Quarry north of the capital creates one Fragment and Sovereign Ward book on defeat. Its leash10 m,2,600 HP and ordinary immunity/telegraphs stay inherited. Heart Foundry southeast of the capital is a vulnerable shared forge reachable from capital, Ashcross and Ironworks. No dynamic resupply produces a second Fragment, catalyst bundle or capstone book.

Both antechamber entrances lead to a dry southern flank and an eastern/northern street. Add a wet western approach through Crown Ford. Inside the final70 m precinct, route one dry walkable inset plus one authored shallow-water lane, each at least4 m wide, around the Heart plaza; the actual Heart standing circle remains dry. The small wet inset is a static water polygon fitted by B90 (12 m×6 m centered(15,10,4 m)); it is not a third macro river or a gate. Every inlet drains visually without making the claim circle a compulsory Wet trap. No full-map global weather is required. A defender can prepare a reaction on the wet approach, while a cautious carrier can choose the dry flank.

There are no shrine healing pads, spawn protection zones or official body collision inside the70 m Heart precinct. Closest basic service recovery is outside it, preserving disengagement cost. The macro atlas shows settlements and tower points; tactical street/window/river-inset fitting is tested in the capital detail and native level.

Actual Stun (including successful Wet–Frost), Hex, explicit Interrupt, lethal damage, nonzero forced movement, voluntary action/hold loss, leaving range or authorization loss cancels a claim. Root/Slow/Wet/Silence/Mute and nonlethal damage alone do not. Guarded Frost that applies no Stun cannot cancel through a fake reaction flag. The ten-second hold cannot include simultaneous item use, trading or attacks. Public claim sensor publishes static objective location/timing; it does not authorize body targeting through fog.

The inherited OR of access routes, inclusive deadline/window endpoints, due-combat-before-completion ordering, owner/life checks, full warning+window feasibility, no refunds and simultaneous claim start-ordinal ties all remain. At exactly5,400 s, due valid completion may win after due combat; otherwise NoAcquisition. No hidden score winner, last-survivor shortcut or forced tower deletion. Death retains progress and ordinary equipped gear, drops keys/artifact/cargo/consumables and uses min(90,8+2×level+floor(elapsed/120)) seconds; the late death may be decisive but is not permanent elimination.

## Pacing hypotheses

0–20 min: establish kit and income locally;20–45: rare wilderness resources and route choices;45–70: towers, work routes, forge and transport contests;70–90: pressure converges on Heart. These are observation bands, not locked content tiers. Cities/services/ruins/towers are available from start; clever early wins remain legal. The demo is unsuccessful as a60–90-minute format if ordinary matches consistently end at10–20 minutes or wander until NoAcquisition—record that outcome and tune progression/placement/telegraphs explicitly.

Default24 contenders is a first test of density, not a promise that5.76 km² supports that count well. Log first rival sighting, repeated travel downtime, route participation and regional heat. Before adding combat bots or movement speed, shorten empty connections with meaningful cover/interactions and revise target distances if evidence warrants. Public warnings need a plausible response from local forward positions, not from every outer city in30 s. Record how many nearby non-activators could physically arrive in time.



---

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



---

# Implementation stages — B90 to B94

Dependencies are strictly B90→B91→B92→B93→B94. A feature is complete only when its exit evidence exists in the native build. Keep actual source commits and test artifacts in the feature ledger. Do not mark a stage done because its actors import, markers appear, or a validator passes.

| Stage | Deliverable | Required exit evidence |
| --- | --- | --- |
| **B90** | Full terrain/road/river/settlement skeleton and spatial importer | All96 POIs and24 spawn pads load with stable IDs; permanent routes, ramps and bridge/floor continuity; native path/travel table and at least two Heart approaches |
| **B91** | Sight, interiors, mutable foliage and object interactions |36/144 enterable;72 doors;480 props;16 traversal anchors; fitted collision/vision/nav agreement; world/item action separation and race tests |
| **B92** | Service economy and living population | Five market pools, shared official stock, finite sources/contracts,196 noncontender-role capacity plus24 contenders; working routines/local security; no duplication/hidden-information AI |
| **B93** | Victory geography and four real routes | Three towers/unique keys/one boss/forge; every route earns access legally; ten-second contest, warnings, drops and endpoint tests in capital |
| **B94** | Full-session and multiplayer validation |60/75/90-minute native scenarios, restricted clients, worst-density stress and human pacing observations with recorded hardware/build/latency |

## B90 — world skeleton

Audit baseline first. Build manifest importer and deterministic native builder; fit terrain bands, ramp networks, river banks/floors, bridge decks and capital wet inset. Place all settlement blocks and POIs before detail. Preserve editable/final fitted transforms and IDs. Implement debug views for route graph, pad bounds, terrain bands, visibility proxies and native navigation. Run routes without any movement ability and with no object mutation. Record native reachability for every POI and two routes to key targets. Zero orphan markers.

## B91 — functional environment

Hook object types into inventory/interaction authority. Deliver one complete reference encounter (Riverport public frontage→rear door→warehouse crate→roof ladder→grass escape), then extend shared components across all instances. Implement static occlusion first, then door/prop/foliage changes with atomic proxy revisions. Add water/oil accessory uses, bounded torch ignition and existing Frost/Steam reactions. Show usable portable-item bindings, fuel and carry state. Include a native encounter in both Wet and dry Heart approaches. No cosmetic-only verbs.

## B92 — population and bounded economy

Initialize five exact market pools, common caches, resource charges and24 roster seats once. Add official counters and village work/recovery without duplicating stocks/rewards. Import152 routine assignments; implement all role capacities and ordinary AI contender legal commands. Audit inherited Witness and starter behavior. Add local witnessed security, muted/quiet behavior, explicit visual-only traveler cargo. Record NPC awake tiers and real accounting identities. Meet zero-kill progression and rival interference acceptance cases with actual receipts.

## B93 — route convergence

Place the three towers, rare ruin caches, single boss and Heart Foundry. Implement goal selection for contender AI without grants; use real acquired goods and access rights. Test a public Charter opening won by someone else; keys dropped/recovered with preserved lock; artifact consumed then owner dies; siege permanence; two simultaneous claimants; exact-window stun versus harmless surface slow. Confirm local guards/shrines/protection do not sterilize the precinct.

## B94 — full-match validation

Run24 locked contenders (human seats may be automated fixtures in soak runs, clearly labeled) at60,75 and90 min. An actual terminal winner is allowed to end a normal run early; long-duration stress fixtures may suppress final claim only in an isolated test mode with no balance claim. Require at least one60+ minute human-involved observed match before calling the full-session pacing validated. Do not equate automated soak with fun or balanced matches.

Targets: client60 FPS at a recorded resolution/quality/hardware; server30 Hz, p95 tick≤33.3 ms and p99≤50 ms; p95 acknowledged world interaction≤250 ms at documented RTT; high-level route-query p95≤10 ms. Stress the capital with24 contenders, local crowds, open doors, moving cargo, smoke/fields and max accepted field allocations; also stress distributed activity. Reject unsafe resource requests without charging; no dropped paid fields, hidden body leaks or claim timing drift. Targets are provisional and unmeasured.

Collect winner/route/time, first rival encounter, level/gold curves, work/camp quotas, first-visit/discovery XP, travel and idle intervals, regional heat, guard pursuits, valid/failed placements, visibility mismatches, late event queues, path failures, active AI tiers and conservation deltas. If most sessions end well before60 min or at NoAcquisition, report the failure and propose measured tuning; never insert an undocumented minimum-win gate or award a score winner.

## Completion report

Fill `templates/IMPLEMENTATION_REPORT.md` with source baseline, architecture reuse, per-stage status, native tests passed/failed/not run, manifests/hashes, screenshots/log paths, remaining defects and tuning deviations. Update `FEATURE_LEDGER.csv`. All v0.8 behavior remains regression scope unless this package explicitly overrides it. New map breadth is not a reason to remove established abilities/items or provide silent fake adapters.



---

# Native acceptance and playtests

62 native cases. All initial statuses are **Not run**. `tools/validate_spec.py` checks package consistency and selected independent boundary models only; it cannot pass these native cases. IDs use the user-required B90–B94 stages.

Record actual evidence and failures in the implementation report; do not edit this source index to imply test execution.

## B90

| Case | Action | Required result | Evidence |
| --- | --- | --- | --- |
| B90-T01 | Import the selected full profile twice, then reset | 96 POIs,144 buildings,24 spawn pads; stable IDs; no duplicate actor/source/market | Import report, source hashes, actor ID diff |
| B90-T02 | Walk without abilities from every spawn to every service and objective | Native path succeeds; no compulsory ladder, bridge-only river gate or invalid slope | Native all-pairs required-target path report |
| B90-T03 | Walk all named and unmarked river crossings thawed and frozen | Same connectivity; Wet at fords, dry deck over bridge; no base river slow | Movement/contact logs and capture |
| B90-T04 | Traverse every road/plateau interface | Continuous <=25% ramp or valid stairs, correct band height, no cliff clipping | Native sweeps, elevation overlay |
| B90-T05 | Inspect routes to each ruin, boss,forge and Heart | Two usable approach branches at key targets; one occluded or alternate branch | Native paths + sightline evidence |
| B90-T06 | Walk antechamber to Heart at420 cm/s | Measured <=15 seconds on normal dry path; claim circle not forced Wet | Path trace and server elapsed time |
| B90-T07 | Visit five cities and six towns | Correct identity/services, six city districts, distinct watched core and edge | Level walkthrough + service mapping |
| B90-T08 | Regenerate seed9001 twice and reopen level | Same semantic IDs, positions,source counts; visual scatter cannot move objectives | Serialized manifest diff |
| B90-T09 | Place all primitive building/road geometry | Local district approach routes fit doorways; no carved road through a closed shell | Native collision and path overlay |
| B90-T10 | Walk near playable border and ledges with push/dash | No falling death, out-of-bounds launch or endpoint inside collision | Sweep and rejected endpoint receipts |
| B90-T11 | Measure prescribed long trips and empty road stretches | Native length/time separated from graph estimate; empty segments recorded for density tuning | Completed travel CSV and heat trace |
| B90-T12 | Switch fresh match to v08_integration | Original short-map limits and sources active alone; no v09 rewards/endowments imported | Profile-specific actor and ledger dump |

## B91

| Case | Action | Required result | Evidence |
| --- | --- | --- | --- |
| B91-T01 | Enter all36 interiors; inspect108 closed shells | Exactly25% enterable; legal real doors/stairs, no fake door verbs | Native interior checklist |
| B91-T02 | Open/close door while another client observes and paths through | Collision,sight and local navigation share one revision; occupied closure rejects | Two-client video + revision receipts |
| B91-T03 | Force/lockpick rear door; attempt lock at public front | Rear broken-open persists; no public-service lockout; owner rule enforced | Door and navigation logs |
| B91-T04 | Two clients take one flask or carry one ladder simultaneously | Exactly one location/owner; losing request spends nothing | Race receipts + item identity query |
| B91-T05 | Throw/pour a flask from unbound inventory then bound accessory under Mute | Unbound/Mute rejects; bound action consumes once with inherited cooldown/mask | Action/inventory receipts |
| B91-T06 | Ignite torch and toggle light through drop/swap/death/rejoin | Fuel never resets; no extra sight/detection or ground auto-fire; Mute respected | Fuel trace + restricted payload |
| B91-T07 | Carry,push,place crate/barrel near door/Heart/corridor and another body | One world instance,70%carry speed,suppressed actions; unsafe placement rejects; no body displacement | Native geometry + receipts |
| B91-T08 | Break an empty and a filled container in a race | Existing contents drop once; no invented oil,material,gold or duplicate storage | Lineage/item identity check |
| B91-T09 | Deploy/retrieve rope and ladder; race traversal; Root/Stun mid-use | One instance/anchor reservation; permanent stairs usable; interruption returns valid endpoint | Link/inventory/state logs |
| B91-T10 | Harvest,burn and attack the same sapling across death/reload | At most one Fiber when harvested; destruction none; proxy removal matches sight/nav | Source receipt + geometry diff |
| B91-T11 | Hide in grass, approach within2 m, detect, attack, then burn patch | Geometry-gated concealment; reveal timers; no wall reveal; burnt cover stays removed | Two restricted client traces |
| B91-T12 | Use Wet+direct Frost repeatedly on river, then Frost Capsule alone | 600 ms stun/6000 ms victim lock; floor only30%slow; guard and death lock rules retained | Combat/contact boundary trace |
| B91-T13 | Create Steam then Gust; attempt unit target across smoke/wall | Union occlusion applied to all audiences; authorization recalculated; no target/private-data leak | Restricted serialized payload inspection |
| B91-T14 | Fill field budget while lighting sapling/oil | Capacity refusal without cost; paid reservations survive; expiry and source ownership exact | Capacity/receipt trace |
| B91-T15 | Fit all480 functional props and3000 scenery-tree targets | No essential corridor overlap; all verbs work; static trees do not burn or drop loot | Fitted manifest + interaction sampling + count report |

## B92

| Case | Action | Required result | Evidence |
| --- | --- | --- | --- |
| B92-T01 | Start default4-human full match then12-human configuration | 196 noncontenders +24 contenders;20 then12 AI contenders; no double roster count | Roster/role budget dump |
| B92-T02 | Observe civilian loops and cross-city travelers for15 minutes | Real path travel, staggered activity,flee/resume; sealed cargo neither loot nor market stock | Routine timeline + stock delta |
| B92-T03 | Attack resident/force rear door inside and outside guard sight | Only observed local incident; no omniscient suspect or human label | Guard knowledge and observer records |
| B92-T04 | Fight/respawn guards and ambient wildlife repeatedly | Zero gold/XP/loot mint;24 guard IDs;90 s downtime; no infinite reinforcements | Kill/source and ledger receipts |
| B92-T05 | Buy/sell simultaneously at all five market pools | Current unit quotes,finite cash,stock caps and source lineage; stale quote rejects | Transaction races + five-pool ledger |
| B92-T06 | Buy Circle-III book at multiple official endpoints | Only two copies per definition globally; no per-city rare stock clone | Stock/source audit |
| B92-T07 | Run all29 production/consumption ticks including empty/poor pool | At most145 producer burn; actual items consumed; no deferred catch-up or hidden funding | Scheduled receipts + identity closure |
| B92-T08 | Complete/repeat/abandon/die during courier and procurement work | 20 paid courier cap,40 issuance cap,one active token; endorsement andN budgets exact | Contract/source receipts |
| B92-T09 | Harvest all deposits,saplings and initial common caches | Finite charges/endowments,no XP/gold mint or life reset; no rare key in common chest | Item-source closure |
| B92-T10 | Progress from zero kills to paid registered Charter with a legal Witness path | Actual native skill gates/costs documented; no free grant; both endorsements and100 gold spent | Complete action and balance trace |
| B92-T11 | Transfer gold,bank across cities,die with gear/cargo/consumables | One account; fees/tax burn; ordinary gear retained; consumables/objectives drop once | Gold and item conservation report |
| B92-T12 | Hide hazards/keys/rival wallet from contender AI | AI choices use owned/observed/public facts; no remote path cost or objective-carrier oracle | AI input audit + restricted view |
| B92-T13 | Move near/far bodies across simulation tiers during combat/transactions | Wake before observation; one identity/receipt; no skipped damage or visible teleport | Tier transition and native movement trace |

## B93

| Case | Action | Required result | Evidence |
| --- | --- | --- | --- |
| B93-T01 | Destroy each tower once then reload/rejoin | Three permanent losses,three relics,total120 gold; siege opens once; local watch removed | Objective/source/access log |
| B93-T02 | Activate Charter near Heart while a different contender waits locally | 30/60 public window; nonbuyer can physically win; Treasury purchase still required | Activation/travel/claim receipts |
| B93-T03 | Collect all keys,activate,transfer/drop/recover one | Unique keys,lease revoked on loss;90 s instance+owner locks persist | Instance and access timeline |
| B93-T04 | Craft/activate Heartbreaker,die before and after activation | Exact inputs/20 fee,8 s craft; artifact drops precommit or is consumed once; no refunded lease | Lineage and life-bound lease receipts |
| B93-T05 | Attempt duplicate ruin,boss,Fragment or capstone reward | Three key bundles,one boss,finite assigned books; restored body cannot pay again | Source identity closure |
| B93-T06 | Hold Heart through ordinary damage,Wet,Root,Silence,Mute and proximity | Surviving authorized claim continues; no passive contest or damage interrupt | Claim cancellation reason log |
| B93-T07 | Apply Stun,Hex,Interrupt,actual push and zero-distance blocked push at completion | Actual qualifying effects cancel; zero movement alone does not; due combat precedes completion | Scheduled-time boundary trace |
| B93-T08 | Use guarded Wet-Frost and frozen surface at a claim | Guarded no-Stun and surface slow do not falsely cancel; lock spent as inherited | Reaction and claim receipts |
| B93-T09 | Overlap access routes; close one exactly at claim end | OR authorization; inclusive endpoint; no timer extension or boolean-close race | Access interval/claim receipt |
| B93-T10 | Start two claims with equal completion time; reach exact deadline | One role-neutral start-ordinal winner; valid due completion before NoAcquisition | Winner receipt + monotonic timeline |
| B93-T11 | Attack,observe and approach Heart without own access route | Defenders enter normally; fog still governs targeting; no civic guard/shrine/protection bubble | Native precinct inspection |
| B93-T12 | Run AI route attempts using real earned resources | No hidden teleport,free key/artifact/Witness,money or scripted tower deletion | Complete contender action/commerce traces |

## B94

| Case | Action | Required result | Evidence |
| --- | --- | --- | --- |
| B94-T01 | Run normal24-contender matches at60/75/90-minute deadlines | Terminal winner or truthful NoAcquisition; no min-win gate; correct timer/profile | Per-run roster,profile and terminal receipts |
| B94-T02 | Run at least one human-involved60+ minute observed session | Record pacing,empty travel,first encounters,route response; no automated-soak balance claim | Session notes + telemetry |
| B94-T03 | Stress distributed whole-map population then capital convergence | Recorded hardware/settings,30 Hz server p95<=33.3 ms,p99<=50 ms;60 FPS target measured | Profiler captures and percentiles |
| B94-T04 | Stress1024 live cells,2048 transitions/100 ms and pending reservations | No paid field loss,resource overrun,late claim/control reorder or expiry drift | Environment and event queue profiler |
| B94-T05 | Test150 ms RTT,packet loss,reordered/duplicate requests and disconnect/rejoin | No stale-life commits,duplicate item/reward or identity leak; p95 interaction target reported | Network harness and receipt audit |
| B94-T06 | Open remote inspector/minimap/listeners behind fog during mutations | No hidden roles,build,wallet,loot coordinates,crime tracker or live fogged proxy changes | Restricted payload diff |
| B94-T07 | Soak carry,door,nav and traversal interactions with crowd pressure | No permanent path deadlock,stuck essential service,vanished cargo or repeated receipt | Path failure/timeout logs |
| B94-T08 | Close every gold and item ledger at terminal/reset | Delta zero; initial+mint-burn accounting includes all market/resident/world locations | Machine-readable closure report |
| B94-T09 | Regress inherited52 abilities/80 items/20 recipes and traits/loadouts | No map-scope deletions,fake ops,unbound item actions or changed combat limits | Inherited test report + importer coverage |
| B94-T10 | Review all B90-B94 exits and deviations | Native evidence attached; failed/not-run gates remain visible; tuning/data/prose synchronized | Completed implementation report and feature ledger |


---

# Coding agent prompt

Implement Project Lockdown v0.9 in the existing Unreal project using this package and its byte-preserved v0.8 source. Deliver a functional complete-map demo with placeholder art where necessary. This is a code/level implementation task; importing documents or drawing map markers alone does not fulfill it.

First read00–10, the active data manifests and inherited v0.8 authority/combat/economy/route contracts. Audit the actual repository, engine version, build/run/tests, current implementation and AGENTS.md. Record mismatches and keep existing architecture/working content. Do not assume a missing system was already delivered. No engine-version-specific API is mandated here.

Use **B90, B91, B92, B93, B94** as stage IDs everywhere. Begin at B90, validate its exit criteria, then advance through the dependent stages. Use B90-Fxx feature IDs and B90-Txx acceptance IDs, similarly for later stages. Commit coherent verified increments with stage IDs if the user's repository instructions authorize commits; report the actual commits and remaining gates. Never make stage names M0/S1 or carry the inconsistent v0.8 convention forward.

Build the2.4×2.4 km seed9001 layout: Crownreach, four outer cities, six towns,96 POIs,144 buildings/36 interiors,480 functional props,16 traversal anchors, permanent walkable rivers, fitted roads/ramps, Wet and dry Heart approaches. All essential locations are reachable at baseline420 cm/s without movement powers or portable traversal. Use deterministic approved placement and stable IDs. Extend the native builder rather than shipping a disconnected web demo.

Activate exactly one profile. v09_full has24 contender seats including1–12 humans; AI fills remaining seats. Preserve the isolated35-minute v08_integration regression profile. Import v0.8 catalogs with explicit v0.9 overlays, never duplicate legacy nodes, stock pools, rewards, rare keys or objective actors. Follow centimeter/millisecond units. Unknown IDs, verbs, effects or data fields that affect behavior fail import.

Reuse server action/inventory/transaction/fog authority. Portable item actives require accessory bindings and Mute checks; the six shared action slots remain shared by combat/profession/gear skills. Heavy props remain world instances; no unbound flask/torch shortcut. Atomic door/foliage/cargo changes update collision, sight and local navigation together. Permanent river connectivity and mandatory dry alternatives survive every optional mutation. Do not replace bounded environment with fluid simulation, universal fire spread or arbitrary physical construction.

Populate actual work/trade/rest/security routines and official services. Cosmetic travelers carry no fake market stock. Real sources and transactions conserve gold/items. Use full-profile quotas and finite endowments; no free gear/money/capstones/Witness for bots. Audit missing inherited Witness behavior before claiming zero-kill Charter works. AI planning uses owned/observed/public information and ordinary legal requests. No hidden opponent or human labels in AI/clients.

Connect all four routes to one real physical ten-second claim. Keep exact access-window/deadline event ordering, drops, control versus damage interruption and no refunds. Move Charter activation to antechamber, retain its Treasury purchase. Ensure civic guards and public recovery cannot immunize claimants. Do not script wins, delete towers on timers or introduce minimum-win gates.

Run `python3 tools/validate_spec.py` for input checks and the native cases from10. Spec/model success is not runtime evidence. Record actual path lengths, multiplayer races/payload authorization, ledger identities, NPC behavior, route legality and performance. Validate60/75/90-minute profiles; clearly distinguish normal matches, soak fixtures and human balance observations. Fill implementation templates honestly. Report Not run or blocked work, exact missing dependencies, real build/test commands and defects. Never claim a complete playable full map if native B90–B94 gates remain unmet.



---

# Provenance, compatibility and limits

Input: the user-supplied `Project_Lockdown_V08_Combat_Elements_Implementation_Spec_Package(1).zip`, locally read in this task and preserved byte-for-byte as `reference/Project_Lockdown_V08_Source.zip`. Its nested v0.7 source was read for visibility, world objectives, claims, economy and inherited v0.6 production/contract semantics. The user supplied the map proposal and accepted its balanced/hybrid/selective-interior choices. The user then explicitly required B90/B91/... stage IDs. No external engine documentation or game repository was supplied or assumed.

v0.8 is authoritative for52 active abilities,80 items,20 recipes, class traits, stat/rank/loadout limits, combat/effects, bounded reaction semantics, inventory/death, exact Heart access and all unchanged catalog definitions. Its old world/profile/spawn/market/economy schedules are active only in v08_integration; v09_full overrides precisely the topics in00 and data overlays. New portable torch/lantern/rope definitions are map additions, not replacements for catalog gear. Ladders remain heavy world objects.

The preserved v0.8 provenance itself describes no verified native implementation. Neither package is evidence that current code already contains the behavior. Library file availability does not imply access to a game repository. This deliverable contains specification data, generation/validation utilities and accurate manifest-derived atlases, not Unreal levels, cooked assets, compiled C++, a shipping AI or measured performance.

All v0.9 numerical selections (24 contenders, population, source budgets, geometry, sight, schedules, cargo constraints, guard tuning, field capacity, latency/FPS gates) are implementation defaults, not experimentally established balance. The actual v0.8 starter wallet/kit and old Witness operation must be audited in the repository; their absence is a named dependency. If actual wallet endowments differ, update initial ledger totals before release, retaining user-authorized starter behavior.

Hybrid geography fixes the reviewed macro map across matches and procedural detail uses controlled seeds. It does not promise limitless content, runtime city regeneration, general physics, physical aggregate merchant supply chains, exhaustive building interiors or90-minute balanced autonomous simulations. Each deferred behavior must remain visibly absent, rather than appearing as a nonworking interaction.

Stage/test IDs B90–B94 are a user requirement. Spec check results, native test statuses and feature completion remain separate. An acceptance-index row initially says Not run until the native implementation provides evidence. Source/outputs use SHA-256 for traceability; hashes prove bytes, not gameplay correctness.



---

# Full POI coordinate index

Coordinates below are meters; JSON stores integer centimeters. Numeric atlas labels match this index.

| # | ID | Name | East | North | Height | Functions | Reward source |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | capital | Crownreach | 40 | 40 | 4 | route_hub; civilian_work_and_trade | None |
| 2 | highfort | Highfort | -820 | 720 | 8 | route_hub; civilian_work_and_trade | None |
| 3 | scholars | Lumen Archive | 780 | 780 | 4 | route_hub; civilian_work_and_trade | None |
| 4 | riverport | Riverport | -800 | -660 | 0 | route_hub; civilian_work_and_trade | None |
| 5 | ironworks | Ironworks | 760 | -680 | 4 | route_hub; civilian_work_and_trade | None |
| 6 | oakmere | Oakmere | -770 | 80 | 0 | route_hub; civilian_work_and_trade | None |
| 7 | millford | Millford | -390 | 450 | 0 | route_hub; civilian_work_and_trade | None |
| 8 | reedbank | Reedbank | -470 | -650 | 0 | route_hub; civilian_work_and_trade | None |
| 9 | ashcross | Ashcross | 380 | -850 | 4 | route_hub; civilian_work_and_trade | None |
| 10 | bellwick | Bellwick | 730 | 200 | 4 | route_hub; civilian_work_and_trade | None |
| 11 | stonewake | Stonewake | 0 | 840 | 8 | route_hub; civilian_work_and_trade | None |
| 12 | capital_treasury | Treasury | -69.2 | -44 | 4 | bank; charter_purchase; core_procurement | None |
| 13 | capital_supply | Official Supply | 3.6 | -44 | 4 | official_supply; circle1_books | None |
| 14 | capital_guild | Civic Guild | 112.8 | -44 | 4 | supply_commission; courier_board; witness | None |
| 15 | capital_workshop | Commons Workshop | -69.2 | 124 | 4 | workshop; preparation | None |
| 16 | capital_infirmary | Public Infirmary | 3.6 | 124 | 4 | shrine; safe_route_information | None |
| 17 | capital_guard | Civic Guard Post | 112.8 | 124 | 4 | guards; crime_reporting | None |
| 18 | highfort_barracks | Barracks | -898 | 660 | 8 | trainer_circle2; preparation | None |
| 19 | highfort_supply | Garrison Supply | -846 | 660 | 8 | official_supply; circle1_books | None |
| 20 | highfort_market | Highfort Market | -768 | 660 | 8 | market; courier_board | None |
| 21 | highfort_workshop | Armorer Hall | -898 | 780 | 8 | workshop; preparation | None |
| 22 | highfort_inn | Cliff Inn | -846 | 780 | 8 | shrine; civilian_rest | None |
| 23 | highfort_guard | North Watch | -768 | 780 | 8 | guards; crime_reporting | None |
| 24 | scholars_archive | Grand Archive | 702 | 720 | 4 | archive_circle3; circle2_books | None |
| 25 | scholars_supply | Scholars Supply | 754 | 720 | 4 | official_supply; circle1_books | None |
| 26 | scholars_market | Garden Exchange | 832 | 720 | 4 | market; courier_board | None |
| 27 | scholars_workshop | Research Hall | 702 | 840 | 4 | workshop; preparation | None |
| 28 | scholars_infirmary | Garden Hospice | 754 | 840 | 4 | shrine; civilian_rest | None |
| 29 | scholars_guard | Library Watch | 832 | 840 | 4 | guards; crime_reporting | None |
| 30 | riverport_market | Harbor Exchange | -878 | -720 | 0 | market; courier_board | None |
| 31 | riverport_supply | Dock Supply | -826 | -720 | 0 | official_supply; circle1_books | None |
| 32 | riverport_guild | Port Guild | -748 | -720 | 0 | supply_commission; courier_board; witness | None |
| 33 | riverport_workshop | Shipwright Hall | -878 | -600 | 0 | workshop; preparation | None |
| 34 | riverport_inn | Low Tide Inn | -826 | -600 | 0 | shrine; civilian_rest | None |
| 35 | riverport_guard | Harbor Watch | -748 | -600 | 0 | guards; crime_reporting | None |
| 36 | ironworks_forge | Advanced Forge | 682 | -740 | 4 | advanced_forge; preparation | None |
| 37 | ironworks_supply | Foundry Supply | 734 | -740 | 4 | official_supply; circle1_books | None |
| 38 | ironworks_market | Foundry Exchange | 812 | -740 | 4 | market; courier_board | None |
| 39 | ironworks_workshop | Open Workshop | 682 | -620 | 4 | workshop; preparation | None |
| 40 | ironworks_inn | Cinder Inn | 734 | -620 | 4 | shrine; civilian_rest | None |
| 41 | ironworks_guard | Foundry Watch | 812 | -620 | 4 | guards; crime_reporting | None |
| 42 | camp_01 | Wilderness Camp 1 | -1030 | 400 | 0 | bounded_combat; patrol_cover | camp_01 |
| 43 | camp_02 | Wilderness Camp 2 | -580 | 920 | 8 | bounded_combat; patrol_cover | camp_02 |
| 44 | camp_03 | Wilderness Camp 3 | 380 | 1020 | 8 | bounded_combat; patrol_cover | camp_03 |
| 45 | camp_04 | Wilderness Camp 4 | 1040 | 510 | 4 | bounded_combat; patrol_cover | camp_04 |
| 46 | camp_05 | Wilderness Camp 5 | -1040 | -180 | 0 | bounded_combat; patrol_cover | camp_05 |
| 47 | camp_06 | Wilderness Camp 6 | -600 | -320 | 0 | bounded_combat; patrol_cover | camp_06 |
| 48 | camp_07 | Wilderness Camp 7 | 460 | 470 | 4 | bounded_combat; patrol_cover | camp_07 |
| 49 | camp_08 | Wilderness Camp 8 | 1010 | -120 | 0 | bounded_combat; patrol_cover | camp_08 |
| 50 | camp_09 | Wilderness Camp 9 | -1020 | -990 | 0 | bounded_combat; patrol_cover | camp_09 |
| 51 | camp_10 | Wilderness Camp 10 | -100 | -1000 | 0 | bounded_combat; patrol_cover | camp_10 |
| 52 | camp_11 | Wilderness Camp 11 | 480 | -410 | 4 | bounded_combat; patrol_cover | camp_11 |
| 53 | camp_12 | Wilderness Camp 12 | 1040 | -980 | 4 | bounded_combat; patrol_cover | camp_12 |
| 54 | ruin_a | Glass Monastery | -960 | 1020 | 8 | unique_key_catalyst_book_cache; ambush_and_retreat | ruin_a |
| 55 | ruin_b | Drowned Observatory | -70 | -1020 | 0 | unique_key_catalyst_book_cache; ambush_and_retreat | ruin_b |
| 56 | ruin_c | Ash Vault | 1040 | -420 | 4 | unique_key_catalyst_book_cache; ambush_and_retreat | ruin_c |
| 57 | boss_lair | Hollow Crown Quarry | 90 | 570 | 4 | single_guardian_boss; fragment_and_book | boss_once |
| 58 | heart_forge | Old Heart Foundry | 390 | -310 | 4 | heart_forge; craft_interception | None |
| 59 | tower_a | Grand Tower A | -90 | 190 | 4 | siege_gate; local_public_watch | tower_a |
| 60 | tower_b | Grand Tower B | 195 | 10 | 4 | siege_gate; local_public_watch | tower_b |
| 61 | tower_c | Grand Tower C | 10 | -155 | 4 | siege_gate; local_public_watch | tower_c |
| 62 | heart | Grand Heart | 40 | 40 | 4 | single_physical_claim; public_attempt_sensor | None |
| 63 | antechamber | Heart Antechamber | 40 | -5 | 4 | keys_activation; heartbreaker_activation | None |
| 64 | north_bridge | North Bridge | -280 | 650 | 0 | route_choice; vision_or_exposure_control | None |
| 65 | mill_bridge | Mill Bridge | -360 | 350 | 0 | route_choice; vision_or_exposure_control | None |
| 66 | capital_ford | Crown Ford | -325 | 100 | 0 | route_choice; vision_or_exposure_control | None |
| 67 | south_bridge | South Bridge | -300 | -560 | 0 | route_choice; vision_or_exposure_control | None |
| 68 | reed_ford | Reed Ford | -300 | -820 | 0 | route_choice; vision_or_exposure_control | None |
| 69 | east_bridge | Garden Bridge | 610 | 295 | 4 | route_choice; vision_or_exposure_control | None |
| 70 | east_ford | Bell Ford | 840 | 223 | 0 | route_choice; vision_or_exposure_control | None |
| 71 | north_pass | North Saddle | -450 | 1020 | 8 | route_choice; vision_or_exposure_control | None |
| 72 | west_pass | Oak Cut | -920 | 360 | 4 | route_choice; vision_or_exposure_control | None |
| 73 | east_pass | Foundry Steps | 960 | -250 | 4 | route_choice; vision_or_exposure_control | None |
| 74 | south_tunnel | Ash Culvert | 320 | -600 | 0 | route_choice; vision_or_exposure_control | None |
| 75 | heart_dry_flank | Heart Dry Flank | 100 | -95 | 4 | route_choice; vision_or_exposure_control | None |
| 76 | crystal_north | Sky Crystal | -250 | 1030 | 8 | finite_Crystal_harvest; trade_route_interception | crystal_north |
| 77 | crystal_west | Moss Crystal | -960 | 100 | 0 | finite_Crystal_harvest; trade_route_interception | crystal_west |
| 78 | crystal_center | Quarry Crystal | 220 | 570 | 4 | finite_Crystal_harvest; trade_route_interception | crystal_center |
| 79 | crystal_east | Garden Crystal | 1000 | 900 | 4 | finite_Crystal_harvest; trade_route_interception | crystal_east |
| 80 | crystal_south | Reed Crystal | -540 | -1000 | 0 | finite_Crystal_harvest; trade_route_interception | crystal_south |
| 81 | ore_north | North Ore | -1080 | 800 | 8 | finite_Ore_harvest; trade_route_interception | ore_north |
| 82 | ore_east | Foundry Ore | 950 | -850 | 4 | finite_Ore_harvest; trade_route_interception | ore_east |
| 83 | fiber_west | Oak Fiber | -980 | -360 | 0 | finite_Fiber_harvest; trade_route_interception | fiber_west |
| 84 | fiber_south | Farm Fiber | 260 | -1040 | 0 | finite_Fiber_harvest; trade_route_interception | fiber_south |
| 85 | oak_shrine | Oak Shrine | -650 | 260 | 0 | recovery; covered_detour | None |
| 86 | reed_shrine | Reed Shrine | -660 | -880 | 0 | recovery; covered_detour | None |
| 87 | ash_shrine | Ash Shrine | 550 | -1020 | 4 | recovery; covered_detour | None |
| 88 | sky_shrine | Sky Shrine | 420 | 850 | 8 | recovery; covered_detour | None |
| 89 | quarry_shrine | Quarry Shrine | 210 | 450 | 4 | recovery; covered_detour | None |
| 90 | west_watch | Broken West Watch | -1050 | 540 | 4 | supply_storage; tactical_vantage | None |
| 91 | east_watch | Abandoned East Watch | 1050 | 80 | 4 | supply_storage; tactical_vantage | None |
| 92 | smuggler_cove | Port Quay and Smuggler Cove | -800 | -805 | 0 | supply_storage; tactical_vantage | None |
| 93 | old_lab | Old Laboratory | 1010 | 1040 | 4 | supply_storage; tactical_vantage | None |
| 94 | fallen_garden | Fallen Garden | 400 | 680 | 4 | supply_storage; tactical_vantage | None |
| 95 | charcoal_camp | Charcoal Yard | 590 | -180 | 0 | supply_storage; tactical_vantage | None |
| 96 | ferry_ruins | Ferry Ruins | -450 | -160 | 0 | supply_storage; tactical_vantage | None |
