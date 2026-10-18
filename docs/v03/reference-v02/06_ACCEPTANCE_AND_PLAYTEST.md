# v0 2 acceptance and playtest plan

## Evidence policy

All tests below are planned requirements, not reported results. Run the real simulation and room paths. Use local-only fixtures for deterministic placement, time boundaries and race conditions. Identify every fixture in the report. Use production tuning for route cost, delayed counterplay and human strategy observations.

Retain v0.1 authority, movement, wall collision, input expiry, malformed message, attack timing, assassination escape, simultaneous damage, valid upgrades, transaction and room isolation tests. Replace expectations for civilian payouts, survivor victory, permanent elimination, indefinite corpses and the lethal safe circle. Do not mark conflicting historical tests as unexplained failures.

## Life and information

| Test | Expected result |
| --- | --- |
| V01 Baseline audit | Repository revision, runnable commands, implemented foundation, missing features and baseline failures are recorded before new feature claims |
| V02 No survivor victory | With one living contender and others waiting, the match continues; all dead also continues until a return, claim or deadline |
| V03 Respawn formula | Level one at zero seconds returns after 20 seconds; level two at 240 after 40; level three at 480 after 105; level three at 719 after 110 |
| V04 Gold loss | Level three with 101 gold loses 31 and retains 70; zero stays zero; replaying the death event applies no second loss |
| V05 Retained and lost state | XP, upgrades, profession, residual gold and counters persist; active job, consumables and preparation are lost; carried Seal drops once |
| V06 New body and protection | Respawn restores upgraded HP with a fresh body ID; old-life requests fail; four-second protection blocks damage and all gameplay interactions and contest |
| V07 Timer snapshot | Death at 479 seconds uses the pre-Showdown formula and its displayed return time is not extended at 480; no post-result return occurs |
| V08 Reconnect and withdrawal | A vulnerable body can die during grace; reconnection restores correct wait or life; expiry withdraws and drops without automatic survivor victory |
| V09 Fog memory | Unexplored and explored-unseen terrain differ; actors, wards, corpses and drops disappear outside current authorized sight in actual serialized payloads |
| V10 Height and trees | A low source cannot see a raised target or through a raised plateau; a high source can see lower ground; trees and walls block both |
| V11 Private ward | A remote ward reveals to its owner and not an opponent; active cap two, expiry 60 seconds, 20 HP and one-second placement cadence are enforced |
| V12 Ward placement | Reject blocked, hidden, out-of-range, overlapping and cliff placement without consuming; accepted duplicate request consumes and places once |
| V13 Public source union | Both clients receive shared tower sight, including remote regions; private sources remain private; camera panning grants no visibility |
| V14 Source loss | Destroying or expiring a source removes now-hidden entities in that tick's outgoing view; no interpolation cache continues to move them |
| V15 Dead-player view | Waiting players receive only public and owned ward sight and no body or free-camera source; their placed wards persist, then expire normally |
| V16 No metadata leakage | Role, human ownership, enemy job, rank, resources, exact hidden carrier position and hidden camp state are absent from normal match payloads |

## Objectives and peaceful growth

| Test | Expected result |
| --- | --- |
| V17 Defensive gate | The central tower takes zero damage until all outer towers fall; same-tick outer destruction does not retroactively permit a protected central impact |
| V18 Tower retaliation | Legal aggressors trigger 12 damage at specified cadence and windup if still in range and sight; passersby are never targeted; last-hit reward commits once |
| V19 Tower reward bounds | A Core adds 20 only to tower basic damage, does not stack with another Core and disappears on death; no PvP or assassination bonus |
| V20 Assault victory | With no Seal, ordinary access stays locked until all four towers fall; a legal ten second claim then wins and freezes the match |
| V21 Claim interruption | Damage on the completion tick, movement intent, release, stale input, death, range loss, other action or another unshielded contender within 120 resets to zero |
| V22 Single terminal result | Two held attempts cannot complete through request order; residents do not contest; protected bodies do not contest; a valid completion at exactly 720 wins before no-claim evaluation |
| V23 Private work contracts | Each contender can accept one independent contract, visits all three destinations per seeded cycle and waits 20 seconds after delivery for another |
| V24 Work reward | Valid three-second collect and delivery channels award exactly 25 gold, 15 XP and one commission once; interruption resets; death cancels active work |
| V25 Rank and craft | Rank two at two deliveries and rank three at six; six deliveries yield 150 gold and 90 XP before spending; four-second crafting deducts 150 and grants once only on successful commit |
| V26 Peaceful bypass | A zero-kill contender earns and crafts a Seal, then claims while all four towers remain intact; other Seal-less contenders remain locked |
| V27 Craft races | Reject insufficient gold, second craft, full carry slot, stale body and replay; damage or death at completion grants nothing and deducts nothing |
| V28 Drop and theft | Death or withdrawal drops exactly one Seal; it outlives the corpse, remains normally fog-filtered, and another living unshielded contender can pick up and use it |
| V29 Pickup contention | Simultaneous legal pickup requests produce one owner via the seeded shuffled order; no role preference, duplicate ownership or two successful receipts |
| V30 Coarse announcements | Craft, drop and carrier district changes disclose district only; no hidden exact position or carrier identity; legitimate Heart attempts create the specified public sensor |

## World and integration

| Test | Expected result |
| --- | --- |
| V31 Resident cover | Residents never attack and award no kill resources; they flee disturbances, complete ordinary routines and return after 20 seconds |
| V32 Public exposure | Damaging a resident, contender or tower in active tower sight exposes the aggressor for 12 seconds; exposure grants no through-fog tracking |
| V33 Camp lifecycle | Two 60 HP creatures use legal sight, eight-damage windups and leash; residents are not targeted; full camp returns 75 seconds after both die |
| V34 Preparation and recovery | Final camp kill gives plus 40 tower-only damage for 45 seconds; Core stacks to plus 60; fountains heal 12 per second only after five quiet seconds and never reach a claimant |
| V35 Economy replay | Medkit and ward costs, inventory, per-match purchase caps, full-HP rejection, death loss and duplicate requests remain authoritative across respawns |
| V36 Map access | All spawns, respawn pads, POIs, cliff ramps, camp retreats and two objective approaches are navigable; initial contenders have required separation; dash cannot cross a cliff or tree |
| V37 Bot fairness | Bots receive the same authorized views and legal action checks; an unseen carrier or hidden human flag cannot steer their target selection |
| V38 Autonomous progress | With a passive connected human and production rules, bots complete actual work or preparation and tower actions, then attempt a valid Heart route; event trace contains no scripted resource or tower grants |
| V39 Market event | Every 90 seconds a seeded resident activity shift occurs; existing private contracts retain destinations; no free objective progress is granted |
| V40 Default multiplayer | Two human operators control independent clients through a shared real server, observe consistent claims, death, return and ownership; multi-context automation is reported separately |
| V41 Deadline and results | No valid claim at 720 produces No claim; there is no gold, kill or survivor winner; results name a winner only from a committed Heart receipt |
| V42 Rematch and isolation | Ten rematches reset all lives, contracts, fog, sources, Seals, towers, camps, timers and generations; another room remains unaffected; empty room grace ends in disposal |
| V43 Performance and delay | Measure full 12 minute eight-client room, all 38 initial moving actors, towers and up to 16 wards; measure actual bidirectional WebSocket delay, not only HTTP throttling |
| V44 Setup and usability | Fresh checkout follows documented setup, two-client demo works, HUD and fog legend are readable at 1280 by 720, and no local test controls are available in normal play |

For tower difficulty, record a normal unprepared attempt and a camp-prepared attempt under production values. The design intends preparation to matter, but retreat, geometry and tower target choice may affect measured damage. Test the arithmetic separately from the playtest judgment.

For V26, a controlled room may use workers instead of challengers to preserve the towers, but label that setup as a mechanical route demonstration. Then test default bot policies or human opposition separately. A fixture cannot establish that the peaceful route is competitively viable.

## Required performance record

Record hardware, OS, browser, build, map and configuration, client and room counts and whether server and clients shared a machine. Starting targets are 95th percentile server tick work below 25 ms with no sustained backlog, median at least 55 FPS and 95th percentile frame time below 33 ms at 1280 by 720, and average outbound below 150 KiB per second per client. Report observed results rather than assuming these budgets hold after adding vision.

Run a full 12 minute room with combat, work, ward turnover, death and returns. Separately test 100 and 150 ms measured round-trip delay with up to 25 ms one-way jitter, inspecting assassination warnings, interruption, claim cancellation and pickup acknowledgements. Keep 95 percent of acknowledgements below 250 ms at 150 ms RTT as the initial target. Use transport-level delay or a verified network conditioner. Record action and snapshot latency separately.

Information leaks, duplicated items or rewards, false victories, skipped death penalties and unusable counterplay are release blockers. Performance misses require a measured diagnosis and documented disposition. Do not call an unmeasured target a pass.

## Human design experiment

Run at least eight complete matches across two sessions with four human players if available. Count a multiplayer design session only with at least two independently controlled humans. Identify bots and policy configurations. Include two controlled route demonstrations, then default-rule matches where players independently choose strategies. Use the same production tuning for comparisons.

Observe peaceful commissioning rate, safe versus wild travel, player-initiated public aggression, tower and camp interactions, ward usefulness, life downtime, Seal losses, attempt interrupts, claim time and route, no-claim frequency and replay interest. Interview players about why they selected a route, what they could see, how they identified danger and whether a death changed their plan.

Initial review questions are:

- Can someone reach a valid Heart attempt without any kill or defensive destruction, and can opponents still contest it?
- Do preparation and tower destruction change the aggressive route meaningfully?
- Do public routines remain credible cover rather than constant fighting?
- Can players explain why a region is visible and what disappeared when a tower fell?
- Are early deaths recoverable and late deaths costly without unexpected timer changes?
- Does a player who loses a Seal retain a plausible plan rather than becoming permanently irrelevant?
- Does the match finish through an understandable claim often enough to justify further content?

Treat fewer than six claims in eight default-rule matches, one route never reaching an attempt, or repeated unexplained vision deaths as iteration triggers. These are proposed review aids for a small sample, not statistically validated balance thresholds. Do not require equal win rates from a tiny group. Record raw counts and explain controlled sessions separately.

If no human group is available, finish engineering validation and state that route competition, public calm and replay value remain pending. An AI win establishes autonomous legality, not player enjoyment. Advance only after reviewing both engineering results and actual human observations; otherwise identify the highest priority change for another iteration.
