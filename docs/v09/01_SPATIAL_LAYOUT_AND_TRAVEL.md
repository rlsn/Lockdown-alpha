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

