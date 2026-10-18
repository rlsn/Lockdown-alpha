# v0 2 gameplay and objectives

## Required loop

Join a private free for all room, blend into ordinary activity, grow through work or risky encounters, invest in vision and recovery, choose a Heart access route and complete an uninterrupted claim. Death costs resources and time but normally returns the contender. Match victory never follows merely from being alive when others are dead.

Use eight contender slots filled by one to eight humans and enough autonomous contenders to reach eight. Keep 24 residents and add three wild camps with two creatures each. A one-human room is practice. Use one shared character kit. The match deadline is 720 seconds after PLAYING begins. At 480 seconds enter Showdown, which increases newly calculated respawn penalties but does not unlock the Heart automatically. At the deadline, resolve the tick and award a completed claim if valid; otherwise return No claim, without a score winner.

## Retained combat and growth

Retain walk 90 pixels per second, sprint 150, actor radius 14, 100 contender HP, 40 resident HP, 20 basic damage, 150 ms basic windup, 750 ms start cadence, 48 pixel basic reach and an 80 degree cone. Retain the 60 damage rear assassination with 500 ms warning windup, 250 ms recovery, 56 pixel reach, 18 second cooldown, four second target damage-history check and one second quiet preparation. Retain dash 180 pixels over 200 ms, seven second cooldown and no invulnerability. All earlier interrupt and simultaneous impact rules remain except where this document explicitly changes life state.

Retain level two at 30 cumulative XP, level three at 80, cap three and the four existing upgrade choices: Vitality adds 10 maximum and current HP, Fleet multiplies walk and sprint by 1.05, Recovery changes future dash cooldowns to six seconds, and Patience changes future assassination cooldowns to 15 seconds. Two upgrade choices maximum, no repeated upgrade, no automatic level bonus. Profession rank is separate and gives no raw combat stats.

All values are proposed tuning. Preserve these combat values initially so the experiment measures objective and vision changes. Structures cannot be assassinated; a basic attack can hit one other visible actor or an attackable structure, nearest centre in the legal cone and reach. Visibility may come from any authorized source, but the melee segment itself cannot cross a wall, tree or non-ramp cliff edge. A remote sensor never allows an attack through an obstacle. Equal distance uses opaque target ID. Do not let a strike cleave a resident and tower together.

## Grand Heart access

The Heart is fixed at the map centre. It is not a damageable actor. There are three outer towers and one central tower. All outer towers must be destroyed before the central tower can take damage. Ordinary Heart capture is allowed only after all four towers are destroyed. Destroyed towers remain destroyed for the match.

A contender carrying a Concord Seal may attempt a Heart claim while any or all towers remain. This is an individual bypass, not a global unlock. Other contenders without a Seal still need ordinary access. Neither earning gold, crafting a Seal nor destroying the last tower wins by itself.

### Claim rules

Within 60 pixels of the Heart, hold E while alive, unshielded, connected and stationary. Select Assault access if all towers are destroyed; otherwise require a carried Seal and label the route Seal. Lock that route for the attempt. A valid attempt takes 10 uninterrupted server seconds. Releasing E, stale input, movement intent, taking damage, death, withdrawal, leaving range or starting another action resets progress to zero. No progress is retained between attempts. A Seal is not consumed on an interrupted attempt.

Any other living, unshielded contender within 120 pixels of the Heart contests the attempt, regardless of access route. Residents and wild creatures do not contest. Contested attempts hold at zero and cannot complete; announce Contested without disclosing hidden coordinates or a participant count. Re-evaluate after movement, damage, deaths and respawns each tick. There is no first-request ownership lock, and no processing-order advantage for simultaneous requests. With two eligible bodies present, neither can progress.

Announce a valid claim attempt, its route and progress globally. While an attempt is held, the Heart supplies shared sight in a 180 pixel radius using the vision rules; retain it for two seconds after the final held attempt ends. This intended public sensor makes the final event observable. Do not disclose a remote claimant's nickname, inventory or exact position beyond authorized sight. The central 160 pixel disk is open terrain so the contest space is readable.

The first valid completion wins immediately and freezes competitive simulation. The Heart is acquired at completion; there is no extraction stage. A Seal used for the winning attempt is consumed. If no claim completes, there is no winner. Do not add survivor, kill, gold or tower-count tiebreaks.

## Tower network and preparation

| Tower | HP | Protection | Shared sensor | Starting reward |
| --- | --- | --- | --- | --- |
| Each of three outer towers | 360 | Attackable from match start | Radius 300, elevated source | 40 XP, 30 gold and a Breach Core |
| Central tower | 600 | Damage immune until all outer towers fall | Radius 300, elevated source | 40 XP, 30 gold and a Breach Core |

Tower HP, shield status and destruction are public objective facts. The source attacker is not automatically revealed outside permitted sight. Tower bases are permanent movement blockers; destruction removes the sensor and active tower but does not change base collision. They do not respawn.

After taking legal damage, a tower can retaliate against living unshielded contenders that damaged it in the last six seconds. Within 240 pixels and its line of sight, target the most recent aggressor, using target ID for equal timestamps. First retaliation may start 500 ms after first legal damage; later starts have a 1000 ms cadence. Each has a 300 ms visible windup and deals 12 damage if the target remains valid at impact. No random projectile or hit chance. A shielded central tower ignores attacks and does not retaliate from ignored damage. Towers do not attack peaceful passersby.

A Breach Core automatically equips on the living credited tower finisher. It adds 20 basic damage against towers only, does not stack with another Core and lasts until death or match end. The wild camp's temporary preparation bonus can stack with it. It does not improve damage against contenders, residents, camp creatures or wards. This is one bounded objective artifact, not an artifact inventory system.

Credit the largest damage contribution due on the tower's lethal tick, breaking ties by target-independent opaque contender ID. A dead finisher receives no reward, but the tower still falls. A tower destruction creates exactly one reward event. Destroying outer towers is preparation for all players, not a private lock owned by the last hitter.

## Peaceful profession and Seal route

The sole prototype profession is courier work. It requires no kill, camp clear or tower damage. At the Guild desk press E to accept one contract. Its destination is one of three public district stations, cycling through a seeded permutation that visits each station once before repeating. The contract and its destination are private. All players have independent contracts; a resident or another player cannot consume the only available job.

At the destination, hold E stationary for three seconds to collect the work package; return to the Guild and hold E for three seconds to deliver. Damage, movement, another action, release or stale input resets that interaction's progress, without consuming the contract. The package is a contract state, not a stealable object. Death cancels the active contract and package with no reward. A 20 second personal cooldown after delivery prevents immediate repeated acceptance. Show remaining cooldown and the next action.

Each delivery grants 25 gold, 15 XP and one completed commission. Rank one is initial, rank two begins after two completed commissions and rank three after six. Rank is retained through death. Residents can perform matching work animations and routes without participating in the player's private contract. Do not add a human-only package icon above a head.

At the Guild workbench, a living rank-three contender with at least 150 gold can hold E stationary for four seconds to craft a Concord Seal. No kill requirement. Reject if carrying a Seal or if that contender has already crafted one this match. Reserve the request ID, then revalidate rank, balance, inventory and craft count at completion. Deduct and grant atomically only on completion; interruption costs nothing. Six paid deliveries provide exactly 150 gold and 90 XP before purchases or death losses, demonstrating a complete zero-kill route in principle.

A contender may craft at most one Seal per match. Losing it does not remove ordinary assault access; another dropped Seal may also be recovered. No extra recipes, work tokens or generic crafting are required. Jobs remain available after tower destruction, although their routes become less observed.

### Seal ownership and theft

Each crafted Seal has an immutable item ID and exactly one state: carried by one contender, dropped at a location, consumed by victory, or removed by match cleanup. A carrier can hold at most one. A living unshielded contender within 40 pixels and line of sight can press E to pick up a dropped Seal; pickup is an atomic transaction. Simultaneous requests use a seeded per-tick shuffled order independent of human or bot role. Reject other requests with Already taken.

Death or withdrawal drops the carried Seal on the nearest navigable point to the body's position. It is independent of the corpse and never disappears with corpse expiry. If invalid geometry prevents a nearby navigable placement, return it to the Guild workbench and log the fallback. No manual drop or voluntary transfer is required in v0.2. Anyone may use a recovered Seal, without the crafter's profession rank.

Crafting and dropping announce the affected district globally. While carried, announce only its current district when that district changes, not precise coordinates, identity or continuous tracking. Dropped locations become exact only when normally visible. This deliberate objective signal creates interception opportunities while preserving fog. Multiple contenders may craft separate Seals; only one Heart exists and only one completed claim wins.

## Death and respawn

Contender death creates a 20 second corpse and changes its persistent contender state to WAITING_RESPAWN. At the death tick compute:

```text
delaySeconds = 20 + 10 * (heroLevel - 1)
               + 5 * floor(elapsedMatchSeconds / 120)
               + (elapsedMatchSeconds >= 480 ? 45 : 0)
goldLost = ceil(currentGold * (0.20 + 0.05 * (heroLevel - 1)))
```

Use the level and time at death, deduct once and display the fixed return tick. Level cap three makes the loss rate 20, 25 or 30 percent. Do not extend an existing timer when Showdown begins. At level three and elapsed time 480 seconds the delay is 105 seconds. A return scheduled after the match ends simply never occurs.

Retain XP, level, selected upgrades, pending upgrade eligibility, gold after loss, profession rank, completed commission count and the match's purchase and craft counters. Lose carried medkit and ward charges, active contract and package, Breach Core and temporary camp bonus. Drop the Seal. Existing placed wards remain until their normal expiry. Cooldowns keep their absolute endpoints through death; do not reset them to grant immediate repeated attacks.

At the return tick spawn with full upgraded HP at a validated respawn point and give four seconds of protection. Protected bodies may walk and sprint but cannot attack, dash, use items, select upgrades, channel, buy, collect, place wards or contest the Heart. They take no damage. There is no manual early protection cancel. After expiry normal rules resume. Use a fresh opaque body ID. Do not replenish consumables on respawn.

Residents respawn after 20 seconds at a valid public route origin, with full 40 HP and no competitive rewards. Their corpse disappears at return. Wild creatures use the camp recovery rules. A tower is neither a resident nor a respawning contender.

## Rewards and recovery

Resident deaths grant zero XP and zero gold. Wild creature kills grant 10 XP and 10 gold to the living credited finisher. Contender deaths ordinarily grant the retained 40 XP and 30 gold, but no reward if less than 10 seconds have elapsed since the victim's most recent respawn or less than 90 seconds have elapsed since the same attacker received a reward from that victim. Exactly 10 or 90 seconds passes the corresponding check. Initial spawn is not a respawn for this rule. Death cost and item drops still apply when reward is suppressed. These anti-feeding windows apply to bots and humans alike.

For simultaneous combat deaths, retain the v0.1 direct-damage credit rule. A dead killer receives no payout. For withdrawal with no direct lethal impact, use the last attacker within six seconds if eligible for credit. Environmental deaths, if introduced by a map defect, do not invent a reward. v0.2 has no shrinking zone damage.

Retain medkits: price 30 gold, inventory one, maximum two purchases per contender per match, 30 HP healing after a one second stationary channel, consumed on acceptance and lost on interruption. Retain full-HP rejection without consumption. Shops now open a purchase panel through E rather than buying a medkit immediately. Also sell ward charges at 25 gold, inventory cap two and maximum eight purchases per match. Start each contender with one ward charge only on initial spawn. Purchases validate and deduct atomically. Death does not reset purchase caps.

Two fountains heal living unshielded contenders and residents by 12 HP per second, up to maximum, within 80 pixels and line of sight, only after five seconds without taking or dealing damage. Healing is tick accumulated, free and not invulnerability. Camps cannot use fountains. Neither fountain is near enough to heal a Heart claimant.

## Session and result rules

Keep the v0.1 ten second vulnerable reconnect grace. Reconnecting restores the same contender and current life or respawn wait. On expiry or clean leave, withdraw the contender: drop its Seal, end active work and claim, and remove an active body after ordinary death consequences. A contender already waiting to respawn pays no second death cost; cancel its scheduled return. Do not spawn a replacement bot or schedule another life for a withdrawn contender. Other participants still need a Heart claim. All-humans-absent room disposal remains operational cleanup after the reconnect window.

While waiting to respawn, a player may inspect their own retained status and public plus owned ward sight. They have no body sight or free camera vision and cannot issue gameplay actions, including upgrade selection. After returning, queued offers resume legally. Results show the winner and winning route, duration, tower state, commissions, deaths, kills and progression summaries. Do not label other contenders with last-survivor placements. Rematch clears all lives, drops, wards, contracts, towers, camp state, exploration, resources and old protocol generations.
