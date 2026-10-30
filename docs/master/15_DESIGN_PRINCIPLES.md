# Design principles and decision tests

These principles guide tradeoffs and content review. Active release contracts govern exact behavior.

## One shared world connects different forms of power

Combat, work, commerce, theft and exploration should affect each other's opportunities. Ask what another contender can observe, contest or benefit from. A recipe earns its complexity when inputs, station and transport create decisions. A private progress bar disconnected from the map needs stronger justification.

## Preparation creates limits; execution creates outcomes

Attributes, knowledge and gear define a toolkit. Limited prepared actions make prediction meaningful. Timing, terrain, observation and response determine outcomes. Reject shortcuts that let an unslotted skill, unbound item or midfight backpack swap bypass the common budget.

## World interactions reuse the character’s toolkit

Never add a keyboard binding for a new world object or interaction. Use a real
item or prepared ability, ordinary basic attack, ordinary pointer interaction,
or eligible POI presence. Right-click handles pickup and doors; left-click opens
storage and traders. A source must retain its physical identity, binding costs
and action limits. Removing F cannot justify a new free interaction ability.

A channel is one issued order. Press once, show progress, continue after release
and stop with S or an actual interruption. Automatic POIs start once on entry
and do not fight the player by restarting after S. Do not make sustained key
pressure a test of commitment: time, exposure, source reservation and risk are
the commitments. See [control policy](../v09/CONTROL_POLICY_OVERRIDE.md).

Standing at a POI expresses presence; merely crossing it during a movement
order does not. Automatic work must wait for the character to stop and must
respect the leave/re-enter latch after cancellation.

An issued order also needs reliable ownership through transient client stalls.
Keep network liveness independent of physical input, retain authoritative
interruption checks, and release reservations when the order expires. An old
heartbeat cannot resurrect a stopped action. Test a brief gap and a real timeout
alongside normal completion; a headless success alone can miss rendering effects.

## Qualitative utility survives numerical growth

Accessible disruption and counters remain relevant. A lower-level defender needs a plausible intervention, though not an equal open duel. Before adding damage or immunity, test whether it removes an entire response category. Powerful equipment may specialize or combine functions; it should not provide every desirable property simultaneously.

## Information needs a physical source

Range, geometry and explicit public rules govern observation. Camera, selection, audio, VFX, target handles and AI respect the same boundary. Detection reveals a concealed location inside valid geometry; it does not expose private sheets or penetrate smoke. Convenience cannot justify hidden-data access.

## Every advantage has a commitment and a response

Author cost, timing, exposure, limitation and counter together. A Charter buys an opening rivals may exploit. An artifact risks components and consumed investment. Siege permanently benefits rivals as well as its destroyer. Responses can include avoidance, preparation or contesting acquisition, not only reactive buttons.

## Geography creates choices without mandatory specialist tools

Public roads, covered approaches, elevation, water and interiors should offer readable differences. Essential locations remain reachable at baseline movement. Doors, cargo and optional traversal cannot seal the only route. Visual promises must agree with collision and sight.

## Ownership and consequence remain real

Resources come from named endowments or bounded sources. Transfers create no wealth; respawn creates no rare goods. A physical instance has one location even when equipped, reserved or installed. AI uses ordinary legal requests. Scenery must not masquerade as an economic system.

Reserve commitment separately from consumption. Beginning timed work can lock
the selected material while leaving its ownership intact; completion consumes
it only with the promised result. Cancellation releases it. Bind quotas to the
persistent contender and match, so another cashier, life or controller cannot
grant the same opportunity again. Communicate the difference between selling
to a funded market and earning a bounded delivery reward: both grant carried
gold, but they have different sources and scarcity.

## Recovery preserves investment and changes the plan

Retained development supports a long match. Dropped property, wallet loss, position and downtime keep death meaningful. Address recovery failures through explicit design changes rather than concealed gifts or free replacements. Observe whether players can find a useful next action after loss.

Server recovery must preserve the same consequences. Restoring a world keeps
spent resources, exhausted sources, damaged objects and lost cover. It must not
mint replacements or refill a surviving item. Match recovery and fresh-match
reset are explicit, separate operations; neither implies persistent power across
competitive sessions.

## The ending is physical and singular

All routes reach one ten-second interaction. Access never excludes defenders from approaching. Warnings communicate opportunity without granting unlawful vision. One authority resolves simultaneous actions and the terminal receipt. No hidden score winner or minimum-win timer replaces stated rules.

## Measure before broadening

Deliver a representative encounter with real decisions before spreading components across the map. Separate engineering, normal progression, performance and human evidence. Empty travel, repetitive work and frustrating control chains are findings to address.

## Feature review

Answer: What decision is added? Who pays, and when? What can an opponent perceive and do? Which existing authority owns it? What survives cancel, death, trade and reset? Can AI do it without privileged data? Which small native test and human observation will evaluate it? Missing answers mean the feature is still a proposal.
