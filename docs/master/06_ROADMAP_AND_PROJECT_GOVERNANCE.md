# Roadmap and project governance

## Release sequence

| Stage | Question | Scope | Evidence needed |
| --- | --- | --- | --- |
| v0.1 foundation | Can real multiplayer support crowd stealth and responsive combat? | Original one-kit server, movement, attacks, crowd, limited growth and short survival matches | Verify implementation rather than assuming the earlier documents were built |
| v0.2 objective experiment | Can assault and peaceful progression compete for the Heart in a changing city? | Respawns, small tower network, one profession and artifact route, fog, wards, elevation, civilian and contender AI, one wild camp type | Both routes win under their prerequisites; no automatic survivor victory; public calm and wild risk are observable |
| v0.3 Unreal migration and scaffold | Can the chosen native platform improve readability and support authority and vision correctly? | Pinned C++ project, 3D top down world, native input and UI, filtered multiplayer, real representative slice and a parity ledger | Cold build, cooked Windows package, two remote-client privacy checks and readable movement and interaction |
| Post-scaffold parity and iteration | Can the native build reproduce the complete v0.2 loop? | Port deferred tower, profession, economy, AI and population systems before adding more routes | Both original routes work end to end under normal tuning, with migration differences recorded |
| Squad prototype | Does cooperation preserve uncertainty and fair information? | Four-player groups, team sight, shared objective credit, team respawn and ownership decisions | Human squads complete objective matches without information leaks or unclear victory |
| Alpha | Does the full match retain tension for 20–30 minutes? | Expanded tower network, limited distinct characters, deeper professions and measured POI content | Repeated human sessions support pacing and counterplay at declared population and delay |
| Public readiness | Can the project operate a fair online game? | Matchmaking, moderation decisions, abuse limits, deployment, recovery, compatibility and budget | Measured capacity, operating costs, release criteria and ownership agreed |

There are no promised dates or assigned staffing levels. The previously proposed v0.2 roster expansion was superseded by the objective experiment. The former v0.3 route expansion is now deferred behind the Unreal platform milestone and full-parity review. A new character or route does not resolve the current presentation and platform problem.

## Development policy

Retain the working parts of v0.1 and replace its incompatible match policies. Build an authoritative objective state machine before expanding content. Keep damage and counterplay stable while measuring the new strategy layer. Implement one peaceful profession and one alternate access artifact before attempting keys, wealth auctions, bosses or a crafting system.

For the current platform milestone, preserve the completed web prototype for comparison and migrate behavior deliberately into Unreal. A representative scaffold slice can use documented fixtures, but those do not establish full profession, tower, economy or AI parity. Native movement and replication replace the old runtime mechanisms without changing the long term Grand Heart direction.

Prefer explicit domain modules: life and respawn, vision, objective network, economy, profession and AI observations. Avoid an all-purpose quest language, ability editor or distributed backend at this stage. Keep proposed numerical tuning in one versioned configuration.

## Risk register

| Risk | Observation | First response |
| --- | --- | --- |
| Work becomes repetitive waiting | Workers report no route or spending decisions | Change destinations and exposure, then reassess profession variety |
| Peaceful route is ornamental | It cannot win while defences remain, or always finishes much later | Verify the bypass contract and compare route time and risk |
| Aggression dominates public life | Most residents are under attack and public routes offer no cover | Remove civilian incentives, adjust AI restraint and inspect surveillance coverage |
| Tower destruction empties information | Players cannot navigate or contest the centre | Improve ward access, elevated routes and public objective signals |
| Respawns remove stakes | Death scarcely delays progress | Adjust time, loss and dropped possessions using observed route timings |
| Respawns remove participation | Early deaths create long idle periods | Reduce early penalty while preserving expensive late returns |
| Wealth creates unavoidable kills | Progression collapses medium lethality | Cap burst and review item combinations before adding more content |
| Objective never resolves | Repeated interruption yields frequent no-claim results | Review capture radius, access costs, return travel and late respawn duration |
| AI decides outcomes invisibly | Bots know hidden inventory or change objectives without valid actions | Enforce observation boundaries and retain event traces |
| Scope exceeds the experiment | Every proposed POI enters the same release | Hold the explicit v0.2 exclusions |

## Ownership and change control

The project owner decides creative direction and advancement. Developers own implementation, setup, verification and honest reports of limitations. Testers supply evidence; a single preference does not automatically become a requirement.

Record decisions with ID, date, source, status, reason, affected documents and a review trigger. Separate direction changes from tuning. Tag test reports with build, map and configuration versions. When changing a rule, update its specification, implementation and acceptance case together.

The current master plan is revision 3. Preserve earlier gameplay and engineering documents as historical development context. Survivor victory, permanent elimination and the shrinking zone remain superseded; the web runtime is also superseded for new platform development. Do not edit synced project `sources/` files.

## Review criteria

Engineering acceptance requires authoritative objective access, respawn and item ownership, filtered vision, valid transactions, repeatable multiplayer and a clean rematch. Human review separately asks whether calm and dangerous regions differ, peaceful growth feels viable, deaths have understandable costs and the final claim is contestable.

Advance only on evidence. If human testing is unavailable, finish the engineering handoff and mark the design review pending. Never present bot sessions as proof of human strategy or enjoyment.
