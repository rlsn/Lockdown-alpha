# World vision and points of interest

## Fog and terrain

**Direction.** Fog of war, sight blocking terrain and trees, high ground advantage, observer wards and POIs that share vision are part of the intended game. The authoritative server must enforce the information rules. Hiding an enemy sprite while transmitting its position does not implement fog.

The world should distinguish currently visible space from explored terrain and unexplored space. Previously explored terrain may remain on the map, but moving actors and dynamic objective items must disappear when sight is lost. Static objective locations may be public knowledge from the start.

Use consistent rules for body sight, ward sight and public surveillance. A simple early height model can let a higher observer see lower terrain while low ground cannot see across an elevated area. Walls and trees should remain blockers unless a specific sight source explicitly overrides them. Prototype elevation is not a claim to reproduce another game's full vision rules.

Do not add miss chances or random damage modifiers to represent high ground in the first implementation. Sight, route access and preparation already provide an advantage that can be tested independently.

## Private and shared sight

| Source | Intended audience | Design consequence |
| --- | --- | --- |
| Contender body | That contender, and later its squad under explicit team rules | Rewards personal scouting and positioning |
| Observer ward | Its owner, and later its squad | Lets a player invest in a route or objective |
| Intact surveillance tower | Every contender | Makes civic space easier to observe and blend through |
| Selected civic or objective POI | Everyone when explicitly active | Supports readable public activity and contested milestones |

Destroyed towers must stop contributing sight. Public knowledge that a tower fell does not entitle clients to receive every actor nearby. Wards need finite lifetime, placement rules, a cap and counterplay. A shared tower's sight must be labelled as shared so players do not believe it is their private advantage.

Squad vision, detection of invisible units, true sight, day and night cycles, smoke and flying vision are deferred until they answer an observed need. Crowd concealment does not itself require a conventional invisibility mechanic.

## POI ecosystem

The user proposed a broad set of map incentives. They belong in the long term plan even when a release implements only a subset.

| POI | Intended role | Early implementation priority |
| --- | --- | --- |
| Profession station | Peaceful growth and routine cover | Essential for the objective pivot |
| Surveillance tower | Shared sight, defensive progression and chaos milestone | Essential |
| Fountain | Recovery that encourages travel and creates an observable destination | Small early version |
| Observer ward location | Private information and scouting investment | Small early version |
| Wild camp | Optional XP, gold and preparation for combat objectives | One camp archetype first |
| Secret shop | Rare purchases and dangerous travel | Deferred beyond the first objective experiment |
| Random upgrade drop | Opportunistic build changes | Deferred until core growth is measured |
| Character-specific reward | Ability evolution comparable to a specialised objective | Deferred until distinct characters exist |
| Rune | Temporary effect and periodic movement incentive | Deferred |
| Boss | Major risk and a powerful temporary or persistent reward | Deferred |
| Grand Heart | Shared final victory objective | Essential |

## Availability and strength

**Direction.** Strong POIs should usually be impractical early because contenders lack the preparation to defeat them, rather than simply failing to exist until an arbitrary time. The Heart's defensive prerequisites are a separate intended exception: direct capture is protected until the relevant network is dismantled.

Prototype strong encounters with transparent HP, retaliation and preparation requirements. Test whether they are hard early; do not assume a stat table establishes difficulty. Avoid introducing so much raw combat scaling that an early scout cannot survive an encounter with a stronger player.

## Map progression

The long term tower network can include a lesser tower in each outer district and three major towers near the centre. This is an expressed example, not a fixed production map count. Destroying outer defences creates milestones, removes shared sight and enables pressure on the centre. Major objective rewards should be valuable and visibly attributable.

Peaceful stations need routes that are safer while surveillance remains, but still usable after a tower falls. Do not close the only profession route when an aggressive player advances. Conversely, a profession route must still expose its final claim to meaningful opposition.

Use navigable alternatives, wardable intersections, elevated lookout areas and tree-lined routes to connect the systems. Validate every respawn point and service location against the final objective's access routes. A single hidden choke that cannot be scouted or bypassed would make the outcome depend on camping rather than strategic choice.
