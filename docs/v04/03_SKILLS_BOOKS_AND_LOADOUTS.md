# Skills books and the shared loadout

## One active budget

Every learned active below uses one of four equipped slots, including skills invoked through an E context menu. A character may know more than four. Knowledge is not activation permission. No separate combat cap or free utility list exists.

Basic movement, sprint, aim, attack, talk, loot, courier work, book reading, preparation and recipe assembly use no skill slot. They do not execute a learned skill. A finite ward item remains an item under v0.3 rules. Skill scrolls, reusable cast-items and passive skills are out of scope.

Slots 1–4 invoke the selected learned active or arm its contextual use. Old Q and Space may be remapped as aliases to a particular equipped slot, but cannot activate Ambush or Step without equipment. Display a mixed hotbar rather than different combat and utility trays.

## Catalog

R is rank 1–3. Attribute letters use their current integer values. All cooldowns begin on accepted activation and persist by skill ID through unequipping, rank change and death. No other attribute changes cadence, warning or cooldown. A skill's active action occupies the action slot; simultaneous spam cannot bypass recovery.

| Skill | Timing and cooldown | Exact outcome |
| --- | --- | --- |
| Step | Travel 200 ms, cooldown 7 s | Distance clamp 360 + 10 times DEX minus 10 + 20 times R minus 1, between 300 and 420 cm; no invulnerability or obstacle traversal |
| Ambush | Windup 500 ms, recovery 250 ms, cooldown 18 s | Damage clamp 60 + 0.5 times STR delta + 0.5 times DEX delta + 4 times R minus 1, between 50 and 80 |
| Spark | Windup 400 ms, recovery 350 ms, cooldown 6 s | Damage clamp 24 + 1.25 times INT delta + 4 times R minus 1 + Channeler bonus, between 18 and 45; one target within 500 cm |
| Patch | Stationary channel 1 s, cooldown 12 s | Self-heal clamp 25 + WIS delta + 4 times R minus 1 + Medic bonus, between 18 and 40, capped at maximum HP |
| Pickpocket | Stationary channel 1 s, cooldown 20 s plus pair window | One d20 check, transferring up to 12 existing gold on success |
| Parley | Stationary channel 2 s, cooldown 8 s | CHA + 2 times R minus 1 + Envoy bonus against a static task threshold |
| Analyze | Stationary channel 2 s, cooldown 6 s | INT + 2 times R minus 1 against a static task threshold |
| Survey | Stationary channel 1 s, cooldown 6 s | WIS + 2 times R minus 1 + Watcher bonus against a visible clue or task threshold |
| Heave | Stationary channel 2 s, cooldown 6 s | STR + 2 times R minus 1 + Hauler bonus against a physical task threshold |

Terms such as times R minus 1 mean multiplication by `(R - 1)`; times DEX minus 10 means multiplication by `(DEX - 10)`. Use the explicit expressions in the JSON companion. Skill formulas produce their bounded result before the action is committed. Snapshot source attributes and rank on acceptance; changing a build cannot alter an in-flight result. Target legality is still rechecked at impact.

## Combat and targeting

Retain basic melee's 96 cm planar reach, 80 degree total cone, 150 ms windup and 750 ms start cadence. Select one nearest other visible legal target at impact; ties use opaque target ID. Walls, trees and non-ramp cliffs block the melee segment. Misses consume cadence. Positive damage during an unfinished windup interrupts it under the existing simultaneous-batch rule.

Step uses native compatible dash movement, travelling in movement or facing direction and stopping at blockers. It can interrupt an ongoing action. It cannot activate during protection or death. Its distance scales but its 200 ms duration and seven-second cooldown do not.

Ambush retains the existing preparation and response rules: an eligible target within 112 cm and a 60 degree aim cone, attacker within 60 degrees of the target's rear, no target damage dealt or taken for four seconds, unexposed attacker walking or slower for a continuous second, no sprint or dash. Lock the target, expose the attacker, warn the target immediately and recheck range, sight, rear, history and life at impact. Do not reject because the accepted windup itself exposed the attacker. Damage or dash interruption consumes cooldown. Scheduled misses retain recovery. Exact overlap has no valid rear bearing.

Spark selects one visible legal living target in a 60 degree aim cone, nearest first. Lock on acceptance and recheck 500 cm range, geometric sight and both alive at impact. It may hit residents, contenders or the wild sentinel, but not structures or items. There is no area effect, random accuracy or homing through a wall. Its visible cast and private target warning must work without revealing an unseen caster's exact position. Damage interrupts its unfinished cast under existing rules.

Patch is self-only. Reject at full HP without consuming cooldown. Movement, positive damage or another accepted action cancels healing without refunding an accepted cooldown. Reading a book or opening a menu does not secretly complete a second channel. These nine actions share one authoritative arbitration path.

## Books and learning

The Guild sells Step, Spark, Patch and Parley books for 40 gold each. Stock is not globally limited in this experiment. A physical book has a unique item ID, an owner or dropped location and a taught skill ID. Capacity is four unread books. Buying and selling are not XP sources; resale is not implemented.

A living unprotected character can read an owned book after six seconds without dealing or taking positive damage, through a three-second stationary channel. Damage, movement, death, stale input or loss of ownership cancels without consuming. At commit, consume once and teach rank one. No skill point is required for the book path. An already-known skill is rejected with the book retained, not converted into a rank upgrade. Knowledge persists through death; an unread physical book drops.

Learning does not auto-equip, activate, increase rank or clear cooldown. A Rogue can learn Spark from a book. Its INT and Spark rank determine performance, not a hero or class multiplier. A library containing five actives still permits only four equipped.

## Preparation UI

Show separate Learned and Equipped sections with rank, class-learning eligibility, point cost, own scaling values and a clear four-slot count. Combat and utility labels can help description but never change capacity. Show a ghost preview of the proposed build and its HP and speed without applying it.

Apply arms the proposed transaction; holding E at the station for two seconds confirms it. Releasing E or losing focus cancels. Unused points are allowed. Empty slots are allowed; duplicate skills are not. Unequipping a skill preserves its absolute cooldown, including a skill just cast before preparation. UI and server both reject contextual use when unequipped, but the server check is authoritative.

Use held input for contextual stationary work, checks, reading and objective channels. Combat skills activate on a press and then follow their specified windup or travel; releasing the key does not cancel an accepted combat cast. The preparation panel and book inventory must expose the required held action clearly. Every channel still uses the retained stale-input and life-generation checks.
