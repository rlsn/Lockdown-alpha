# Production skills and NPC demand
## Gathering and geography
Keep City, WestTown, EastTown and wilderness. Put stable supply and Core procurement at City, Fiber emphasis in West, Ore in East and two Crystal outcrops at WildCrossing. Use existing public-road and covered routes, body sight and normal navigation. Record actual travel times and exposed versus covered choices.

Each outcrop has three match-scoped charges, no respawn. A living unprotected body within 120 cm and sight can hold F stationary six seconds to extract one Crystal. A successful commit consumes one charge and creates one owned instance only if capacity allows; losing a race or interruption grants nothing and consumes no charge. Normal gather yields no gold or XP. A public depleted state is legitimate. No repeated per-player reset.

## Two light recipes
| Recipe | Inputs | Gold burned | Held duration | Output |
| --- | --- | --- | --- | --- |
| Toolkit | 2 Fiber + 1 Ore | 4 | 4 seconds | 1 Toolkit |
| Core | 2 Toolkit + 1 Crystal | 6 | 6 seconds | 1 Core |

Workshops exist in all three settlements, with these same recipes. They are basic recipe interactions, not extra learned skills. Select explicit owned input IDs, preview cost/net bag space, close panel and arm a fresh Hold F like preparation. Require alive/unprotected, six seconds without dealing/taking positive damage, 120 cm and body sight. Reserve inputs on accepted channel; retain ownership/capacity until completion. Gold is not protected escrow. Movement, damage, stale hold, death, source/range loss or item loss cancels and releases reservations with no spend.

At commit validate inputs, recipe revision, fee/balance, coupon if used, net capacity and current life. Consume exact inputs, burn fee and create output with parent lineage in one transaction. Failure changes none. Full bags may craft if input removal makes room. No crafting XP, durability RNG or reverse recipe. A Core's undiscouted stable-input cost is 4*4 + 2*6 + 12 + 2*4 + 6 = 54 gold when Crystal costs 12.

## Existing skill integration
Every specialized use requires learned/equipped skill, normal rank/attribute score and accepted cooldown, even through F. Use the existing skill path; no new ability or free utility slot.
- Survey score at least 12 can extract one visible Crystal charge through its normal one-second channel instead of the basic six seconds. Capacity/charge race rules are identical.
- Analyze score at least 14 can channel its normal two seconds at a workshop to plan the next craft: one private workshop-bound coupon reduces that craft's fee by 2, expiring after 30 seconds. One outstanding coupon per contender, no stacking; a newer valid plan replaces it. Consumed only by a successful craft, revalidated at commit, and cannot reduce a fee below zero. Reject craft before starting if the selected coupon cannot remain valid through scheduled completion. Death clears it; no trade/XP.
- Heave and Parley retain Charter alternatives; Heave also provides the physical work identity without requiring new numerical production bonuses. Scholar can reach Analyze threshold by actual allocation/rank, Rogue starts a Survey path, and Laborer has its existing peaceful work/Witness access.

All classes can use basic production without these optimizations. No universal CHA discount. Retain learned skills and XP on death; carried materials/outputs drop. Structured Parley tasks do not require dialogue.

## Institutional demand and readable NPC behavior
A marked official consumer at West consumes one market Fiber, East one Ore and City one Toolkit at t=180,360,...,1440 seconds. Each successful consumption removes one actual market instance to a final-consumed sink; no gold changes and no replacement goods appear. If empty, skip that tick without debt/catch-up. This is eight scheduled opportunities per institution in the normal profile.

Provide a visible work routine and public next-tick/status cue near the service. At a due tick, a three-second work animation accompanies the already committed consumption; do not delay/repeat the transaction for animation. Log empty skip. Official consumer policy uses institutional stock, not opponents' hidden inventories. These marked noncontenders are deliberately not a full autonomous economy. Ordinary residents remain peaceful; no new hostile farming population is required.

## Bounded background production
Before consumption at each t=180,360,...,1440 tick, West may create one Fiber into its market by paying 2 gold from its actual reserve to an abstract producer (a gold sink); East similarly creates one Ore for 3 gold. Skip if cash insufficient or stock cap reached, with no deferred debt. Maximum eight creations per producer. Producer payments leave tracked circulation; do not also credit a spendable hidden producer wallet.
Then apply the listed West Fiber/East Ore/City Toolkit consumption. Production and consumption are distinct receipts even when their net stock change is zero. Visible producer/consumer animation represents aggregate transactions, not physically interceptable cargo. Full caravans and NPC supply chains remain deferred. This avoids new pathfinding requirements and explicitly accounts for replenishment rather than granting hidden stock/cash.
