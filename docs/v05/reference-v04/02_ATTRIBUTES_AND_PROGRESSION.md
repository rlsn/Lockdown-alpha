# Attributes and character progression

## Starting classes

Each start has 60 integer attribute points, two rank-one learned and equipped skills, zero gold and XP, and no unspent development points. Appearance is assigned independently. Class is a starting preset, not an immutable combat role.

| Class | STR | DEX | CON | INT | WIS | CHA | Starting skills |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Laborer | 12 | 9 | 12 | 8 | 10 | 9 | Heave and Patch |
| Rogue | 9 | 12 | 9 | 10 | 12 | 8 | Pickpocket and Survey |
| Scholar | 8 | 9 | 10 | 12 | 9 | 12 | Spark and Analyze |

Attributes range from 8 to 16 in this profile. Each earned point increases one attribute by one. No primary stat, per-class automatic growth or hidden efficiency multiplier exists. All start distributions and coefficients are proposed tuning.

## Levels and currencies

| Character level | Cumulative XP | Reward on entering |
| --- | --- | --- |
| 1 | 0 | Starting preset |
| 2 | 60 | Two attribute points and one skill point |
| 3 | 150 | Two attribute points and one skill point |
| 4 | 270 | Two attribute points, one skill point and subclass eligibility |
| 5 | 420 | Two attribute points and one skill point; level cap |

Crossing multiple thresholds grants each reward once. Cap XP at 420 after granting level five. Unspent currencies persist through death. Do not grant random legacy upgrades as well. Profession work uses the same character XP; commission credits are objective receipts, not a second character-level tree.

Class learning costs one skill point for a not-yet-known eligible skill at character level two or later. Laborer can train Heave, Patch or Step; Rogue can train Pickpocket, Survey, Ambush or Step; Scholar can train Spark, Analyze, Parley or Patch. Already-known skills cannot be learned again. Rank 1 to 2 costs one skill point; rank 2 to 3 costs two. All learned skills, including universal book skills, may improve under the same rank rules.

## Derived values

Let an attribute delta be its current value minus ten. Use these formulas only, unless a particular skill specifies another term:

| Value | Formula |
| --- | --- |
| Maximum HP | clamp 100 + 5 times CON delta, minimum 75 maximum 150 |
| Basic damage | clamp 20 + 0.75 times STR delta, minimum 14 maximum 28 |
| Walk speed | 180 times clamp 1 + 0.01 times DEX delta, minimum factor 0.90 maximum 1.10 |
| Sprint speed | 300 times the same DEX factor |
| Basic cadence | 750 ms; does not scale |
| Basic windup | 150 ms; does not scale |
| Body sight | 720 cm; does not scale with WIS |

No passive regen, armour, universal cooldown reduction, cast-warning reduction, mana system or random crit is added. CON does not grant objective interruption immunity. INT, WIS and CHA gain concrete skill or check effects in the catalog rather than universal multipliers.

On maximum-HP recalculation retain current HP clamped to the new maximum; increasing CON grants no immediate healing. A return from death restores full derived HP. Rank changes do not heal, cast a skill or reset an existing cooldown. Test allowed allocations, not impossible all-16 builds, when reviewing combined damage.

## Preparation and allocation

Changes to attributes, class learning, skill rank, subclass and slots commit at a Preparation station while alive, unprotected and out of combat for six seconds. Use one two-second stationary confirmation channel with an expected build revision. Combat means dealing or taking positive damage. Movement, damage, stale hold, death or leaving 120 cm cancels without spending.

Validate the whole requested change atomically at completion. Reject overspending, out-of-range values, duplicate skills, illegal class learning, skipped rank costs, excess slots or stale revisions. A cumulative rank purchase must pay every intermediate cost. No partial spend or silent reorder. There is no attribute refund, class change or subclass respec in v0.4.

## Subclasses

At level four choose at most one of the class's two options through the same preparation commit. Choice is free, permanent for the match and private. It adds no active slot and teaches no free skill.

| Class | Option | Exact effect |
| --- | --- | --- |
| Laborer | Hauler | Add 2 to Heave environmental check score |
| Laborer | Medic | Add 3 to Patch healing before its cap |
| Rogue | Cutpurse | Add 1 to Pickpocket roll bonus |
| Rogue | Watcher | Add 2 to Survey check score |
| Scholar | Channeler | Add 3 to Spark damage before its cap |
| Scholar | Envoy | Add 2 to Parley check score |

Optional means the player may defer choice. It does not mean the implementation may omit selection, validation or effects. These small options test branching without a large subclass content tree.

## Death

Use a bounded profile-specific delay, in seconds:

`delaySeconds = min(60, 10 + 4 * (level - 1) + 5 * floor(elapsedSeconds / 300))`

Compute it from level and time at death. Level one at zero returns after 10 seconds; level five at 1200 seconds after 46. Gold loss is ceiling of 15 percent of current gold, using integer arithmetic. A balance of 101 loses 16 and retains 85.

Retain level, allocated attributes, unspent points, class, subclass, library, ranks, loadout, witness and paid-work counters. Cancel the active job. Drop unread books and a carried Seal; they are separate from a 20-second corpse. Keep absolute cooldowns and retry windows. Return with a new life ID, full derived HP and the retained four-second protection. Protected bodies cannot act, prepare, collect or contest.

This replaces the old short-prototype death formula for v0.4 only. It is not the final long-session balance. Withdrawal applies no second death loss if the contender was already waiting.
