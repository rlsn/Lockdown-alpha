# City and NPC systems

## Population roles

**Direction.** The city needs both nonviolent and potentially violent NPC behavior. Earlier interest in NPCs pursuing player-like goals is retained for autonomous contenders, while the later emphasis on peaceful cover governs the civilian population.

| Role | Purpose | Normal behavior | Winning eligibility |
| --- | --- | --- | --- |
| Residents | Maintain believable crowd cover | Work, travel, trade, rest and flee danger | No |
| Autonomous contenders | Create competing strategies and progress the match | Pursue professions, resources, vision and the Heart using legal player actions | Yes |
| Wild threats | Provide optional combat risk and resources | Guard or patrol a limited wilderness area | No |
| Service characters | Make civic services understandable | Operate shops, profession stations or fountains | No, unless a later release deliberately changes the role |

These are design categories, not mandatory visual labels or separate classes in code. Residents and contenders can share appearances. A player should infer suspicious intent from behavior rather than reading a role flag. Wild monsters may be visibly distinct so optional combat risks are legible.

## Residents and stealth cover

Residents should use places with a reason to visit: markets, workplaces, homes, fountains and civic stations. Routines need variation in speed, pauses and order, but enough regularity that a player can learn them. Panicked residents flee or seek help; they do not automatically become armed competitors.

Civilian murder must not be the default growth engine. **Proposed direction for development:** remove routine civilian kill rewards and provide a strong peaceful income stream. Witnessed aggression should create information and risk. Respawning residents replenish cover so early violence cannot permanently empty the city.

Do not make profession animations or carried work props unique to humans. A tester watching a work station should still need to assess behavior. Conversely, do not force every resident to perform the full competitive economy if it adds complexity without improving the illusion.

## Autonomous contenders

AI contenders pursue real goals, earn legal resources, pay death costs and can win by obtaining the Heart. Their reduced strength should come from limited reaction, imperfect decisions or conservative planning, rather than hidden knowledge or exemptions from mechanics.

Their information is limited to the observations permitted by their body, wards and shared public vision. They may remember previously observed events for a bounded time. They must not select targets through a hidden human flag, know an unseen carrier's precise position or teleport to an objective.

Include distinct strategic tendencies before adding sophisticated tactics. A worker prioritizes legal profession progress, an explorer pursues wild opportunities and a challenger pressures defences. These are policies over the same legal actions, not new character kits. All should avoid purposeless public fights and react to immediate danger.

## Wild threats and public order

Place hostile encounters where surveillance is scarce and routes offer counterplay. Camp creatures should have a leash and an understandable reset so they cannot be dragged across the city into routine civilian slaughter. Public safety need not require a full policing simulation in an early build. Shared sight, fewer civilian incentives, AI restraint and exposed aggression can establish the first safety gradient.

A later release may add guards, bounties, crimes or faction rules if the simpler system fails. These systems are deferred. Do not add them merely to compensate for an AI policy that attacks everything it sees.

## A city that changes without human intervention

**Direction.** Events should happen across the city and meaningful progress should be possible without a human initiating it. Candidate activities include market movement, supply requests, rune appearances, camp recovery, NPC work and autonomous objective attempts.

Use seeded events with visible results and a bounded cadence. Avoid unrestricted event stacking that makes public life unreadable or changes every destination while a player is en route. A meaningful objective transition must still result from valid simulation actions, including AI actions where appropriate.

Measure this through a passive-human session: residents keep working, autonomous contenders complete jobs or contest POIs, resources change and a route toward the Heart advances. Movement alone is insufficient evidence of a progressive world. Also inspect the public streets: the same test must not devolve into constant indiscriminate attacks.

## Questions for later releases

The exact civilian density, the proportion of AI contenders, profession variety, regional factions, service availability and public law response remain open. Decide from observed crowd credibility, travel risk and simulation cost. Document how much of an apparent city economy is simulated and how much is presentation; avoid claiming a complete economic simulation when only routines exist.
