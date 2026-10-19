# Checks theft and transactions

## Deterministic world checks

Static public tasks specify a skill, threshold, position and reward receipt. The character must know and equip the skill, be alive and unprotected, be within 120 cm and have geometric line of sight before attribute evaluation. Missing equipment is not bypassed by clicking E.

The Civic Charter has four alternative services: heavy-goods work with Heave, a mechanism with Analyze, negotiation with Parley, or a field report with Survey. Each uses threshold 12 and that skill's check score. Completing any one grants a private permanent Charter Witness receipt. Further alternatives grant no duplicate reward. They grant no XP or gold. This is four ways to satisfy a prerequisite, not four mandatory classes.

The task UI may preview the character's own score and the static threshold. If the score is insufficient, reject before consuming cooldown. An eligible accepted channel consumes its normal cooldown. Recheck life, range, sight, target state and completion validity at commit. Interruption grants no receipt.

Survey acts only on a currently visible clue or task. It does not scan hidden actors, reveal their library or extend geometric sight. No attribute, rank or subclass bypasses wall, tree or elevation authorization.

## Pickpocket

Eligible humanoids, including contenders and ordinary residents, share a server wallet capability. Do not make a success or rejection message identify a human. Public bodies use neutral observed IDs; stable wallet and contender IDs remain server-only.

Require Pickpocket learned and equipped, a living unprotected actor and target, 100 cm planar range, body line of sight, attacker in the target's rear 120 degree total sector, target not recently exposed and no actor channel conflict. Reject self-targeting. The one-second channel is stationary for the attacker. Recheck eligibility at completion. Damage, movement, stale input, target loss or death cancels. A failed legality check produces no random draw.

On accepted start set both the skill's 20-second cooldown and a 90-second attacker-target retry window. These persist through respawn, slot changes and failed or cancelled attempts. Pair identity is stable, not the temporary body ID. A second accepted attempt is possible exactly at the relevant deadline, never before.

At a valid completion draw one uniform integer 1–20 on the server:

```text
attackerBonus = floor((DEX - 10) / 2) + PickpocketRank
                + (Cutpurse ? 1 : 0)
DC = 12 + floor((targetWIS - 10) / 2) + (targetAlertActive ? 2 : 0)
success = roll + attackerBonus >= DC
```

There is no automatic natural-one failure or natural-twenty success. With DEX 12, rank one, no subclass and target WIS 10 without alert, success requires 10–20: 11 of 20 outcomes, or 55 percent. Tests can enumerate outcomes; normal clients must not receive the target's exact WIS, DC or purse value through a hover preview.

Success transfers min of 12 and current nonnegative target gold, subtracting and adding atomically. An empty wallet transfers zero; it does not create money or roll again. Failure transfers nothing, marks the target Alert for 30 seconds and exposes the attacker for four seconds under ordinary sight. Victims receive an appropriate private tampering or balance-change cue without an unauthorized attacker coordinate. NPCs need no full guard or crime simulation in this release.

Theft grants no XP. Residents begin with 36 gold in their persistent wallet; death and return do not refill it. Contenders begin with zero and use their actual gold. Wallets can be depleted. Do not turn an NPC respawn into a fresh purse.

## RNG contract

Use an authority-only RNG interface with a private production seed and a separate Theft domain. Draw only at the valid completion commit. Receipt ID, outcome and transfer are stored once. Replaying the request returns the original acknowledgement without a second draw or effect. Distinct IDs cannot bypass retry windows. Do not expose a production seed or next outcome in client assets, RPCs or logs delivered to players.

Tests inject fixed rolls or a recorded private seed. Their controls are unavailable in the normal profile. Cosmetic randomness, NPC paths and book item IDs cannot advance the theft domain. Reproducible tests do not establish cross-engine bitwise determinism or cryptographic guarantees.

## Item and economy invariants

Books and Seals have exactly one state: owned, dropped, consumed or cleaned up. A dead carrier drops each physical item once on valid ground. If placement is impossible, use a logged Guild fallback. Corpse cleanup never destroys the item record. Pickup requires a living unprotected character, free capacity, 80 cm and sight; simultaneous requests use the existing seeded role-neutral arbitration.

Book purchase, read, preparation, Charter Witness, delivery, fabrication, death loss and Heart victory use distinct idempotent receipts. Crafting cannot deduct currency if item creation fails. Damage on a completion batch cancels a channel before its reward. A new life cannot replay an old body's request.

Exposure endpoints only extend. Cooldowns never shorten through repeated requests, preparation or rank changes. Profile reset is the only normal operation clearing match-wide reward, retry and crafting limits.
