# World, map and spatial information

## Topology and identity

The selected full-demo map is 2.4 × 2.4 km: Crownreach near the center, four outer cities, six towns and connected wilderness. A perimeter ring and cross-links allow outer-to-outer travel without requiring the capital. Central spokes support convergence without making all early activity a single choke point.

Major geography is fixed and learnable. Seeded detail fills constraints; normal matches do not shuffle rare targets. A layout revision is an explicit design change. The [atlas](../v09/atlas/01_World_Atlas.png) and [v0.9 spatial contract](../v09/01_SPATIAL_LAYOUT_AND_TRAVEL.md) own exact coordinates. B86's small City/WestTown/EastTown map remains an isolated regression region.

| Region | Identity | Choices |
| --- | --- | --- |
| Crownreach | Institutions, terraces, Heart | Treasury commitment, staging and multiple precinct approaches |
| Highfort / northwest | Garrison and ridges | Fortification, exposed high ground, northern rare-source journeys |
| Lumen Archive / northeast | Knowledge and gardens | Books, observation and eastern crossings |
| Riverport / southwest | Waterfront, exchange, warehouses | Cargo exposure, infiltration and canal routes |
| Ironworks / southeast | Ore and forging | Inputs, specialization and vulnerable production journeys |
| Towns/wilderness | Intermediate decisions | Recovery, route changes, camps, materials and ambushes |

## Spatial grammar

Roads indicate predictable public travel. Trails provide cover or alternate approaches without an automatic speed bonus. Lowland, 4 m terrace and 8 m ridge create positions with readable ramps and side ledges. Ordinary paths use continuous fitted slopes or real stairs. Visual cliff walls must agree with collision.

Rivers are permanently walkable Wet floors. Bridges are permanent dry decks; fords use the floor. Ice changes surface conditions and speed, never connectivity. Optional traversal provides opportunity but cannot be required for services, rare targets or Heart. Essential access survives every permitted mutation.

The 96-POI catalog includes hubs, city services, camps, three ruins, one boss, one Heart forge, three towers, Heart/antechamber, crossings, deposits and landmarks. A POI supports multiple purposes but need not create a separate reward pool. Decorative houses and duplicate counters cannot duplicate sources.

## Concealment and cover

Grass creates an escape and ambush decision without changing the route graph.
Entering authored cover can hide a body; moving inside it remains possible.
Leaving, attacking or casting gives opponents a readable opportunity before
cover rearms. An observer close enough to see through valid geometry can find
the occupant; a detector extends that option without penetrating walls or smoke.
Active invisibility retains its own detection rules.

A Torch can permanently clear a named patch at a real fuel and exposure cost.
That loss of cover also benefits later rivals. The plant supplies no destruction
reward and never regrows from a contender's death. Render the cover's footprint
and tall blades while keeping the own character readable; a solid visual wall
would falsely suggest occlusion and frustrate control. The [B91 grass report](../v09/B91_GRASS.md)
records current native coverage separately from the complete reference encounter.

Cover loss belongs to the match's world state, independent of the caster or the
short-lived fire effect. The [settled environment checkpoint](../v09/B91_CHECKPOINT.md)
now preserves cleared patches, exhausted saplings, partial damage and exact
physical goods across a server restart. Full live-match recovery remains open.

## Buildings and streets

The full-demo target is 144 shells with 36 enterable buildings, exactly 25%. Selected services use sparse navigable interiors with public fronts and private rears. Sixteen roof anchors have permanent stairs. Windows are authored openings; roof fading changes rendering while occlusion persists. Closed shells show no fake interaction affordance.

The representative encounter is Riverport frontage → rear entry → warehouse container → roof route → grass escape. It should demonstrate collision, sight, ownership and action decisions before those components expand across the map. B90 establishes structure; B91 owns interactive doors, interior completion, foliage, cargo and portable traversal.

Optional vertical routes change exposure and timing without making a carried
tool mandatory for reaching an essential location. An approved ladder or rope
opens a bidirectional route that rivals may also use. Deployment commits real
property; retrieval requires ownership, quiet and a free link. Permanent stairs
must survive every deployment and retrieval. A roof route should be legible as
a change of position and risk, with readable landings and interruption behavior,
not a shortcut that clips a body through a solid facade.

The [completed B90 structure](../v09/B90_COMPLETION.md) binds every settlement and
service declaration to native floors and paths. Five cities have six service
districts each; six towns have one selected service building each. A town's
authored route hub and its building seat are separate spatial addresses within
one settlement. Aliases reference existing sites and never mint another source.
Road paint stops at walls, all 72 doorway openings admit the native body, and
closed interiors reject navigation. F6 shows this geography and the civic
core/edge policy. B92 must implement actual witnessed security and transactions
against those addresses.

## Sight and uncertainty

Physical obstruction and sight obstruction are distinct design choices. A low
fixed fixture can shape a walking route and hold property while leaving bodies
visible across it. Its public silhouette does not reveal private stored goods.
Use these fixtures to create readable local movement decisions without turning
every solid object into opaque concealment or an implied resource reward.

The full-profile defaults are 18 m own-body sight and 24 m public watch, clipped by geometry. Lower ground cannot see across a tagged upward ledge. Higher ground sees downslope only within ordinary range/blockers. Height grants no automatic range or damage multiplier.

Walls, trunks, doors and smoke have simplified proxies consistent with movement. Grass conceals inside otherwise valid geometry; close observation or authorized detection can reveal it. Detection never penetrates smoke. Camera panning and static map knowledge create no sight. Public infrastructure shares only its explicitly authorized local observations.

## Terrain as a tactical surface

The map supports ground commitments as well as paths. A consumable can prepare a
small crossing, make a retreat slower or leave fuel for a later paid ignition.
Its footprint must follow supported terrain and respect walls, height changes
and observation. Reuse readable terrain materials and crossing choices so the
player can anticipate the result before spending a scarce item.

A bridge has two distinct experiences: the deck remains a dry route while the
water underneath remains a contact hazard. Optional roof access creates another
observation and approach choice; it grants neither extra range nor permission
to affect a different floor through solid geometry. Local support queries must
retain that distinction when a large map replaces the small integration arena.

## Travel and density

Small finite resource blockers create a choice between time and clearance.
Quiet harvesting commits time to recover a material; chopping or fighting a
sapling clears the route without paying that reward. Losing a body cannot
restore a spent tree. Keep the source and resulting item's identities connected,
so recovery and trade move existing property rather than repeat the harvest.
Burning is a separate paid, bounded environment interaction, not automatic fire
spread or an excuse for every tree to become a renewable reward source.

At 420 cm/s, 100 m takes about 24 seconds and 1 km about four minutes before detours. Journeys are a strategic cost. Record native path length, estimated travel, walked time, stops and hazards separately. A graph connection is not a native navigation proof.

Test for recognizable major choices every 45–90 seconds outside settlements and useful smaller interactions along routes. These are hypotheses, not movement rewards. Empty stretches, repetitive safe commutes and compulsory choke points call for fitting or layout revision before adding acreage/speed. Rare targets need two usable approaches; Heart needs Wet and dry alternatives.

The [B90 traversal record](../v09/B90_TRAVERSAL.md) now provides 31 actual walks at baseline speed. Native navigation can take legal open-terrain shortcuts: Lumen Archive→Crownreach measured 252.67 s against a 323.72 s graph estimate, while Riverport→Crownreach measured 263.07 s against 369.73 s. Place useful opportunities along observed paths as well as authored road junctions; a graph alone does not predict player traffic.

The longest measured interval without a POI anchor within 50 m was 339.07 s on Highfort→Lumen Archive. This explicit geometric proxy omits cover, functional props and encounters. It flags a B91 placement priority without proving how a finished route feels. Retain trace corridors and remeasure after meaningful content is fitted.

The [native travel heat plot](../v09/evidence/B90Completion/traversal/TravelHeat.png)
weights each sampled corridor by elapsed walking time in 50 m cells. Its 31
fixture journeys are a reproducible design diagnostic, not a population traffic
forecast. Compare the longest gaps and actual shortcut corridors when placing
functional content; do not interpret the hottest cell as predicted player demand.

The [B90 approach inspector](../v09/B90_APPROACHES.md) now checks seven rare or
forge targets against their native paths. All have distinct local alternatives,
but three of the 36 branch pairs merge near the destination. No sampled
in-range cross-branch ray is blocked by current geometry. Distance can put an observer
outside sight radius; that is not cover. B91 fitting should add readable cover
and observation choices along actual approaches without closing permanent
access. Recheck both paths and sight after those additions.

## Damaging finite obstacles

A weapon attack can spend time and reveal its user to clear a finite sapling.
Ranged attacks travel through the world and respect intervening geometry; they
are not instant object interactions. Damage produces no harvest reward. Keep
tool-based harvesting distinct from destructive clearance, without adding a
world-interaction key. [Current ranged coverage](../v09/B91_RANGED.md) uses a
diagnostic build until normal full-map weapon acquisition is available.
