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

