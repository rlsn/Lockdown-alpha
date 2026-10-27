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
