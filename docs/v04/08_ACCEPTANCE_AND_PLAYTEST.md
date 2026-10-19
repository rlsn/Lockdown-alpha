# Acceptance and playtest plan

These are planned checks, not reported results. Use pure helpers for arithmetic, actual authority transactions for state and independent native clients for observation. Record build, engine, data version, profile, hardware, scenario and actual evidence. Retain the v0.3 movement, fog, ownership, packaging and cleanup checks where unchanged.

## Functional checks

| ID | Required result |
| --- | --- |
| C01 Baseline | Actual Unreal revision, engine lock and prerequisite results are recorded; missing v0.3 work is explicit |
| C02 No heroes | Normal creation uses class presets without primary-attribute or automatic growth fields; model choice does not lock a kit |
| C03 Starting budgets | All three vectors total 60, stay within 8–16 and grant exactly their two rank-one starting skills |
| C04 Level rewards | Cross 60, 150, 270 and 420 XP, including several thresholds in one event; grant each reward once and cap correctly |
| C05 Allocation | Legal spend changes selected attributes only; overspending, cap violation, negative values and stale revision change nothing |
| C06 Derived values | Baseline all-ten attributes give 100 HP, 20 damage and 180/300 speed; STR 12 gives 21.5 damage, CON 12 gives 110 HP and DEX 12 gives 183.6/306 speed |
| C07 No free healing | Increasing maximum HP does not heal current HP; decreasing a maximum clamps; rank and subclass changes do not cast or heal |
| C08 Class learning | Level and class-list eligibility, one-point cost and duplicate rejection are authoritative; book skills can improve normally |
| C09 Proficiency | Rank 1 to 2 costs one and 2 to 3 costs two; a legal cumulative spend cannot skip a cost or exceed rank three |
| C10 Subclass | Level-four eligibility, own-class options, one permanent choice and each of six effects work without extra slots or free skills |
| C11 Shared slots | Combat and utility together occupy at most four distinct slots; a library of five does not permit five activations |
| C12 Bypass attempts | Old Q, Space, context E, direct SkillId RPCs and test wrappers cannot use an unequipped active |
| C13 Preparation | Two-second station commit requires life, range, quiet state and expected revision; interruption spends nothing and cannot partially apply |
| C14 Cooldown identity | Unequip, rank change, slot reorder, subclass, death and new body do not clear an active skill cooldown |
| C15 Book purchase | 40 gold and free capacity produce one instance; replay or insufficient funds cannot duplicate or overspend |
| C16 Cross-class reading | Rogue learns Spark at rank one from a real book, without auto-equip or activation; duplicate knowledge retains the book |
| C17 Reading and drops | A valid three-second read consumes once; interrupted read retains the item; death drops unread books but keeps knowledge |
| C18 Basic scaling | Real melee uses derived damage with retained cadence, geometry and no cleave; no old upgrade multiplier applies again |
| C19 Step | DEX and rank change bounded distance only, with native collision, 200 ms duration and seven-second cooldown |
| C20 Ambush | Rank and attributes change bounded damage without shortening warning; rear, quiet, history and interruption remain effective |
| C21 Spark | Rank and INT change damage, one-target range and sight recheck work, and Channeler applies before cap; no hidden-origin leak |
| C22 Patch | WIS, rank and Medic affect capped self-heal; full-HP rejection, damage and movement cancellation work |
| C23 Witness alternatives | Heave, Analyze, Parley and Survey pass the same public threshold with suitable builds and grant only one Witness |
| C24 Geometric authorization | Check preview, Survey, theft and spells cannot target through forbidden geometry or gain sight from WIS |
| C25 Dice arithmetic | Enumerating 1–20 for DEX 12 and rank one against WIS 10 produces exactly 11 successful outcomes; no natural-roll override |
| C26 One random commit | Accepted valid completion draws once; duplicate request, invalid completion and preview cannot reroll or advance the stream |
| C27 Retry windows | Global 20-second skill cooldown and 90-second stable pair window survive life and slot changes; exact boundary is allowed |
| C28 Bounded theft | Transfer at most 12 existing gold atomically; an empty purse creates zero; concurrent steals cannot spend the same balance twice |
| C29 Consequences | Failure applies Alert for 30 seconds and four-second exposure without revealing an unauthorized actor position |
| C30 Wallet persistence | Resident's initial 36 is not refilled by death, body change or repeat interaction; human and resident validation is role-neutral |
| C31 Region data | Main graph is connected, public and covered routes work and towns connect without the centre; a different node count loads the same rules |
| C32 Exploration receipts | Three first visits grant 25 XP each once through real traversal, not camera, ward, respawn or repeated entry |
| C33 Work budget | Eight paid contracts maximum, alternating towns, proper three-second holds and 20-second acceptance gap; each pays 25 gold, 45 XP and one credit once |
| C34 Combat XP | Sentinel sight, windup, leash, reset and return are valid; only living credited finishers within the two-reward budget gain 45 XP and 25 gold |
| C35 Peaceful XP | Eight deliveries and three visits reach level five without a kill; trades, book reads, failed checks and theft grant no XP |
| C36 Real sabotage | Equipped Analyze score 14 disables public sight for 30 seconds, respects a global 60-second target lockout and restores the source without changing the Heart defence |
| C37 Fabrication | Witness plus four credits and 100 gold produces one Seal atomically; no duplicate craft, free seed or interrupted deduction |
| C38 Earned acquisition | Normal zero-kill play earns the Seal and wins through the real ten-second channel while central defence remains; no fixture grants |
| C39 Theft of objective | An earned dropped Seal can be taken and used by another contender without the crafter's Witness; ownership never duplicates |
| C40 Life and costs | Level one at zero waits 10 seconds; level five at 1200 waits 46; 101 gold loses 16; no double loss on waiting withdrawal |
| C41 Build retention | Allocations, points, subclass, library, ranks and slots persist through death; protection, fresh body and stale-request checks remain |
| C42 Private builds | Restricted clients receive no opponent class, six stats, library, ranks, loadout, points, wallet or secret DC through payloads, actors, widgets or effects |
| C43 Shared NPC rules | A trained NPC's Patch uses learned, equipped, ranked and cooldown checks through the same authority path; residents remain nonviolent |
| C44 Roster and reset | New guests cannot reset budgets after start; verified resume, if retained, restores the same contender; restart clears all match-scoped state |
| C45 Native delivery | Cold build, real asset bootstrap, cooked Win64 game and two independent remote-client scenarios work on the declared toolchain |
| C46 Network conditions | Real 100 and 150 ms RTT checks preserve commitment, warning, spend, roll and view semantics; delayed old revisions cannot restore hidden data |
| C47 Stability | Normal 25-minute run and separate 90-minute harness soak report actor, memory, timers, RNG and receipts; overrides are identified |
| C48 Honest report | Completed ledger separates real new behavior, retained foundation, fixtures, deferred ports and deviations; unrun checks and human evidence remain labelled |

## Boundary and balance tests

Test exact attribute and skill caps, fractional damage, same-batch death and completion, point awards after cap, duplicate read, two pickups, competing thefts, disconnected channels and old-life requests. Enumerate allowed spend budgets when checking combined burst. No single v0.4 skill should kill a full-health valid starting contender. Test earliest legal combinations separately and preserve victim response opportunities.

Verify both a class-trained and a cross-class book version of a skill. The same attributes, rank and subclass effect should produce the same result regardless of starting class. Test all six subclasses using a deterministic fixture, then demonstrate a real level-four choice through progression.

Keep test grants outside the normal profile. The real acquisition and peaceful leveling demonstrations use normal XP, gold, jobs, books, checks and times. If a demo needs extra currency, it does not pass those cases.

## Performance and network evidence

Record engine, CPU, GPU, memory, resolution, scalability, shader warmup, server topology, human clients and NPC count. Starting targets retain median 55 FPS or better and 95th percentile frame time below 33 ms at 1080p, and project authority-plus-view work below 10 ms at the 95th percentile per 50 ms authority interval. Measure both total native traffic and filtered-view traffic; target the latter below 100 KiB per second per client on average.

Test authority acknowledgements at measured 100 and 150 ms round-trip delay, with up to 25 ms one-way jitter. Initially target 95 percent under 250 ms at 150 ms RTT. Use native transport shaping, not browser HTTP settings. Inspect owner-private state and each restricted remote client against a separate server world; a listen host's canonical memory is not a privacy test.

Run ten restarts and the harness-only 90-minute stability case. Do not claim it validates sixty-minute progression, balance or player retention. A required build or package blocked by missing tools remains incomplete, not passed.

## Human review

Run at least six normal-profile multiplayer matches across two sessions with at least two independently controlled humans; four where available. Include two builds from the same class, a cross-class learner, a utility-heavy preparation and a combat-heavy preparation. Avoid forcing every person to follow the same route.

Record point and book choices, equipped utility versus combat skills, route choices, positive and negative theft results, downtime, witness method, source loss, time to each level, acquisition attempts and result. Ask players which capability surprised them, which prepared skill displaced another and what they would change after a setback.

Iterate if contextual utility is effectively free, a starting class lacks a viable peaceful path, everyone uses the same loadout, private build knowledge is obvious from appearance, or deaths and scaling remove counterplay. Report small-sample raw counts rather than asserting equal class win rates.

Full 60–90 minute human validation is explicitly pending. If humans are unavailable, complete engineering evidence and state that build diversity, risk, pacing and enjoyment have not yet been established.
