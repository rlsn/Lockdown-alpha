# v0 2 world vision and AI

## Map contract

Retain the 2400 by 1600 world and centre `(1200, 800)`. Adapt the v0.1 map rather than adding multiple maps. Use three districts with loops and at least two approaches to every strategic area. Keep a clear, flat, obstacle-free disk of radius 160 around the Heart. The final access routes cannot require walking through an unbreakable tower base.

| Object | Proposed centre | Layout constraint |
| --- | --- | --- |
| Heart | 1200 800 | Open contest disk |
| Central tower | 1200 600 | Outside the open disk; shared elevated sensor |
| Outer towers | 500 400 and 1900 400 and 1200 1300 | Each covers its district work station |
| District work stations | Within 80 of each outer tower | On navigable ground with two approaches |
| Guild and workbench | 1000 750 | One civic interaction panel with job and craft choices |
| Shops | 800 800 and 1600 800 | Retain two approaches |
| Fountains | 650 800 and 1750 800 | Outside Heart healing reach |
| Wild camps | 450 1100 and 1950 1100 and 1200 200 | Outside normal tower surveillance, with navigable retreat |
| Respawn pads | 300 800 and 2100 800 and 650 1350 and 1750 1350 | Never inside Heart contest radius |

Positions may shift modestly for valid geometry; record the final map definition and validate all distance constraints. Include at least one meaningful elevated lookout and two static tree clusters in each district. Ramps provide legal access to high ground. Trees and walls block movement and sight; no cutting, destruction or dynamic doors. Tower bases use a permanent 48 by 48 movement rectangle and do not independently block sight. Actors do not block one another.

Initial eight contender spawn points must meet the earlier 250 pixel separation rule. Respawns choose among the four pads: prefer a pad with no living hostile contender within 220 pixels; otherwise choose the pad with greatest minimum distance, using server RNG for exact ties. This safety choice may use canonical positions as an explicit spawn rule, but sends no nearby enemy information. Residents use validated public route origins. No respawn point may be inside a wall, tree, tower base or inaccessible region.

## Authoritative fog and elevation

Represent terrain using 40 pixel cells with height zero or one, plus static wall and tree blockers. A source sees a target point if it is within source radius and a ray does not cross a wall, tree or terrain cell higher than the source height. The target cell is included. A low source therefore cannot see a raised target or across a raised plateau; a height-one source may see across lower terrain. Walls and trees block sources at both heights. A boundary touch counts as blocked. Elevation changes movement only at validated ramps; do not allow movement across a cliff edge.

Body sight radius remains 360. Ward sight is 280. Tower sight is 300 from height one. Heart claim sight is 180 from height one while active. Body and ward height come from their terrain cell. Public towers and the Heart sensor are explicitly elevated sources. High ground changes sight only: there is no miss chance or damage bonus.

For each contender, current visibility is the union of its living body's sight, its placed wards and all active public sensors. A respawn-waiting contender has no body source but retains ward and public sight. A withdrawn contender has no player connection or view. No squad union exists in this release.

Compute current cell masks from cell-centre visibility and retain explored cells separately. Dynamic entity visibility requires both its cell in the current mask and a successful exact ray and radius check from at least one authorized source. Render a soft fog edge so coarse cell boundaries do not imply visibility the server did not grant. Static POI locations and terrain data can be public map knowledge, but a client must not receive dynamic entities outside authorized sight.

Unseen space is dark. Explored but currently unseen space shows terrain without actors, corpses, wards, dropped Seals or camp units. Public tower HP, destruction and Heart activity are authorized global objective facts. A previous sighting may remain as an explicitly stale static POI record, never as a moving actor marker. A district-level Seal announcement is an authorized coarse event, not a position source.

Snapshots can include visible entities anywhere in the authorized union, not merely near the body. The camera may pan through the map or jump to owned wards using the minimap; it grants no new sight. Dead players may inspect authorized sources but cannot reveal another region by moving the camera. Display active shared sight distinctly from private ward sight in a legend or overlay.

## Observer wards

Every contender begins with one ward charge on its first spawn. Buy further charges from shops under the gameplay limits. Press F to place on a navigable, currently visible cell within 48 pixels and clear body line of sight. Reject cliff faces, blockers, a point within 24 pixels of another ward, or an active-owner count of two. Placement is instantaneous on acceptance, consumes exactly one charge and has a one second placement cooldown. It cannot occur during protection, death or another channel or action; a legal dash may interrupt a channel instead.

A ward lasts 60 seconds, has 20 HP and can be destroyed by one basic attack. It does not block movement or reveal its owner's identity. Any visible ward can be attacked, including one's own, but grants no resources. Own wards appear on the owner's map even when the body is elsewhere because they are sources themselves. Enemy wards are sent only when visible through authorized sight. Ward sight is never shared with opponents. Placed wards survive owner death but are removed at withdrawal or match cleanup.

## Public calm and observed aggression

Residents never initiate combat. Autonomous contenders avoid unprovoked resident attacks and do not choose random fights in public surveillance. There is no full crime, guard, bounty or fine system in v0.2.

For an attack that deals damage to a resident, contender or tower at a point visible to an active tower sensor, expose the aggressor for 12 seconds rather than the ordinary four. Set the expiry to the later of its existing endpoint and the newly applied duration; a later four-second cue cannot shorten surveillance exposure. The exposure cue still requires normal authorized visibility; it does not become global tracking. Camp combat outside surveillance uses ordinary exposure. All earlier local disturbances remain direction-only and do not reveal a hidden source coordinate. Public observation plus the absence of civilian kill rewards is the first test of safer civic areas.

Residents use public work, market and fountain destinations with the same walk and sprint speeds as contenders. They pause, turn, work and travel with seeded variation. A local disturbance within 220 pixels causes three seconds of flight along a valid route, then a return toward routine. Civilians never chase or damage another actor. Include matching work animations so watching a station alone does not identify a human.

## Wild camps

Each of three camps starts with two visibly hostile creatures, each with 60 HP. They remain within 240 pixels of the camp centre and acquire the nearest visible living unshielded contender within 160 pixels of the camp centre. They do not target residents. Once engaged, they may pursue that target only while it remains within the leash and visible to their body sight. Use the same terrain sight rules and a 250 ms reaction floor.

Each creature's melee deals eight damage, has 40 pixel reach, a 300 ms visible windup and a 1500 ms attack-start cadence. Validate range and sight at impact; damage and dash counterplay remain normal. Leaving the leash ends pursuit. After eight seconds without a valid target, living camp creatures return and heal to full. No creature can bring a permanent fight into the Guild.

Each creature death grants its normal 10 XP and 10 gold reward once. When both are dead, grant the living credited finisher of the second death a Breach preparation buff: plus 40 basic damage against towers for 45 seconds. It does not stack with another camp buff; a new clear refreshes duration. It may stack with the plus 20 Breach Core. No bonus affects damage to players or other creatures. A full camp returns 75 seconds after the second death, resetting both creatures together. Do not independently respawn the first creature early.

The purpose is to prepare for tower retaliation. Compare an unprepared attack against a prepared attempt in tests. Do not add boss loot tables or generic combat drops. Camp state outside sight is not a globally announced objective fact.

## Autonomous contenders and world activity

At match start assign empty-slot bots worker, explorer and challenger policies in a seeded round-robin distribution. Each can change priority to defend itself, recover, retrieve a visible Seal or contest an announced Heart attempt. Workers complete the same six commissions and crafting process as humans. Explorers pursue camps and preparation. Challengers prepare and damage the tower network. Every policy can use a fountain, buy items, place wards and claim the Heart when legally eligible.

All decisions use public objective facts, private self state and the filtered observations delivered by its permitted sources. Bot planning cannot inspect another contender's rank, inventory, active job, human ownership or hidden position. A district Seal announcement permits travelling to that district but not targeting the unseen carrier. Reaction to a new threat takes at least 250 ms. Navigation can know the static map as players can; it cannot know dynamic hidden bodies.

Use a 250 ms decision interval, bounded path planning and explicit recovery from blocked routes. Avoid resident targets unless a documented defensive accident occurs; do not turn every bot into a civilian farmer. Reduced reaction and imperfect target choices provide a deliberately modest opponent without hidden stat advantages.

Every 90 seconds trigger a seeded market shift that redirects a subset of resident routines toward a different public station and produces a district activity notice. Do not invalidate active human job destinations. Camp recovery and NPC contender goals provide further autonomous changes. These events do not damage towers or award resources by script. With a passive human present, a bot must be able to progress a legal Heart route and eventually attempt it. Log the actions that caused each milestone.

## Input context and feedback

Retain WASD, Shift, mouse aim, left-click attack, Q assassination, Space dash, R medkit and upgrade keys 1–3. Add F ward placement and contextual E. E selects, in order, a visible dropped Seal within 40 if the carrier slot is free, the Heart within 60, then the nearest civic station or shop within 60. Equal civic distances use POI ID. At the Guild, a carried work package makes held E start delivery directly; otherwise the panel shows current job status, Accept contract when eligible and Craft Seal when eligible. Selecting Craft arms its held E channel. A shop panel offers the two purchases. A work station begins its held interaction only for the assigned contract. Panels do not pause the world; Escape closes them.

Show HP, gold, XP, level, upgrades, consumables, active contract stage, profession rank, commissions, respawn countdown, four-tower state, ordinary Heart lock, personal Seal access, Showdown timer and claim alerts. Explain what is shared sight and what is private. Keep essential information readable at 1280 by 720. New POI states must not rely on sound or colour alone.
