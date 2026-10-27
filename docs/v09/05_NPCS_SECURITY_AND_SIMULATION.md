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

