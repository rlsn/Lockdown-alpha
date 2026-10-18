# Grand Heart and match rules

## One victory objective with multiple approaches

**Direction.** Obtaining the Grand Heart is the only normal way to win. Direct central capture is one route, while a demanding peaceful progression can provide access even while the defensive network remains intact. Kill count, net worth, last living status and time survived do not award victory on their own.

This reconciles the two recent ideas: the central objective remains the common goal, and players do not all need to destroy the same towers to obtain it. An alternate route should change the prerequisites to a Heart claim rather than quietly introducing an unrelated score victory.

## Candidate routes

| Route | Progression | Contest and consequence | Status |
| --- | --- | --- | --- |
| Assault | Dismantle outer and central defences, then claim the exposed Heart | Public milestones, combat commitment and lost surveillance | User direction; small network in v0.2 |
| Profession and artifact | Complete peaceful work and spend substantial resources to construct access to the Heart | Travel, visible final attempt and potential theft of the artifact | User suggested; selected as proposed v0.2 alternate route |
| Unique keys | Collect three distinct map keys that drop on death | Multi-location contest, theft and information management | User suggested option; deferred |
| Wealth | Accumulate enough gold to secure access | Economy, spending tradeoffs and exposure at redemption | User suggested option; combined with profession crafting in the prototype |

The alternate routes are candidate designs, not a commitment to ship all of them together. Each must be viable without the mandatory violent milestones of the assault route. They may still involve danger from other competitors.

## Direct capture and defensive progression

The direct route starts protected. Outer defences must fall before the centre can be dismantled, and the required central defences must fall before ordinary capture is allowed. Powerful early POIs should resist unprepared attackers. The broader design includes a lesser tower in each outer district and three central major towers; exact counts depend on the final map.

A Heart claim requires a visible commitment, with the user suggesting about ten seconds of capture. Release specifications must define whether contested progress pauses or resets, what interrupts it, how multiple claimants resolve, and what observers learn. Merely standing on the objective while dead, shielded or disconnected cannot win.

The current proposed interpretation is an uninterrupted claim that can be interrupted and resets when contested. It makes the final action understandable and permits both access routes to converge. Other capture models remain possible after observation.

## Death and respawning

**Direction.** Players and relevant NPCs return after a time penalty. Gold is lost and the penalty grows with level. Late returns can be too slow to influence the final claim, creating practical elimination from that contest without a permanent-death rule.

Calculate the respawn duration from the level and match state at death; show it immediately. Later phase changes should not extend an already displayed timer unless that rule is explicitly adopted. Residents can use a simpler respawn cycle, while autonomous contenders follow player rules. Do not apply level-based contender logic to a tower or camp accidentally.

Spawn protection must prevent immediate repeated death without granting a protected objective claim. Retained progression, cooldown handling and item drops must be explicit. A dead contender can lose the race while waiting even if their scheduled return has not occurred.

Disconnects are session events, not a death escape. A brief reconnect window can preserve the vulnerable body. A participant who withdraws is not a last survivor winner, and remaining participants still need to claim the Heart. Room disposal when nobody remains connected is operational cleanup rather than a competitive victory.

## Ending the match

A successful Heart claim ends the simulation's competitive portion and produces one winner or winning squad. The Heart is not a second item to extract in the current proposed interpretation. If extraction is later desired, it changes the win contract and requires its own specification.

A development time cap may produce a clearly labelled no-claim result. It must not silently return to survivor or score victory. If no-claim outcomes are common, diagnose access costs, contest persistence, travel, respawns and autonomous progression before adding a forced winner.

Full match duration remains 20–30 minutes. Shorter release tests are justified when they exercise both access routes, costly deaths and the final claim repeatedly. A shorter test does not establish the full game's pacing.
