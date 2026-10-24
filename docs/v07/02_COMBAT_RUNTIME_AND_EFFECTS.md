# Combat runtime and effect rules

## One authoritative action pipeline

Reuse the project's actual ability and movement architecture. A single dispatcher governs skill, item, basic attack and contextual actions; widgets cannot apply effects or spend resources. Request includes match, persistent owner, life, action sequence, build/inventory revisions, action ID and an authorized target/point. Arming/preview does not spend anything.

1. Validate alive, unprotected, own life, equipped skill or quick-referenced active item, natural gate, capstone limit, statuses, mana/charge, family/instance cooldown, range, geometry and targeting. Unit targets must be currently observed handles. Ground casts may deliberately target unseen static space.
2. Accept windup; show authoritative cast cue. Stop voluntary pathing unless ability explicitly permits moving (none of the initial cast windups do). Revalidate dynamic target range/sight at release. Movement, relevant disable, Stop/focus loss/death cancels pre-release. No full cost on cancellation, but 400 ms recovery prevents feint spam.
3. At release, deduct listed MP/consume consumable and set cooldown atomically. Channels pay on channel start. Spawn the real projectile/anchor/zone or resolve instant action. An invalid release spends none and enters recovery. Once released, a spell block/miss never refunds mana or cooldown.
4. On impact, reject invalid life/target categories, apply reflection/dome or one spell block, then control guard, then ability effect array in its listed order. Spell block cancels the entire targeted spell packet; basic attack damage itself is not a targeted spell. A valid hostile targeted item is a spell packet for block purposes.
5. Apply mitigation to each damage component, barriers then HP; accumulate simultaneous combat results before final death. Same-batch due actions from bodies alive at batch start may trade kills. Root/grounding prevent forbidden voluntary movement, not remote push/pull. Pure damage bypasses ordinary resistance but still hits barriers; no initial skill uses pure damage.
6. Expire effects/anchors and refresh derived stats/authorized views. Produce a causal receipt and owner-readable feedback. Stale input, duplicate request or old-life projectile cannot create a second outcome. Released projectiles may hit a target in its original life after caster death; never retarget the target's new life.

Combat time uses integer authoritative milliseconds and the existing server scheduling cadence. No client timestamp rewinds visibility, cleanses, claims or deaths. Packet delay is latency, not permission to backdate a counter.

## Status contract

| Effect | Blocked actions | Dispel | Heart effect |
| --- | --- | --- | --- |
| Stun | Walk, attack, skill, active item | Strong, applied externally/preemptively | Interrupt |
| Silence | Active skills and skill channels | Basic | Does not interrupt generic Heart interaction by itself |
| Mute | Active items and item channels | Basic | Does not interrupt Heart by itself |
| Root | Voluntary walk/dash/blink/gate travel | Basic | No interruption if stationary; can stop approach |
| Disarm | Basic attacks | Basic | None |
| Slow | Strongest movement reduction, cap 60 percent | Basic | None |
| Break | Explicitly tagged breakable passives | Strong | No blanket gear-stat suppression |
| Reveal | Concealment within detector's geometric authorization | Source-bound | No identity leak |
| Blind | Own-body sight reduced to 200 cm | Basic | Other authorized sensors remain |
| Hex | Attack/skill/item/mobility; walk at half speed | Strong | Interrupt |
| Displacement | Instant swept push/pull or validated swap | Cannot retroactively dispel | Actual position change interrupts |
| Grounded | Dash/blink/gate travel inside emitter geometry | Source-bound | Does not block ordinary walk or forced push |

Same status instances retain independent source/expiry but use strongest magnitude at any time. Reapplication never adds durations. Example: 1.2 s stun at t=0 and 1.2 s at t=0.6 produce continuous disable until t=1.8, not 2.4. Dispelling removes eligible active instances; it does not suppress an enduring source aura, which may immediately reapply until exited/destroyed. Duration rank growth is small. Status resistance is not added in v0.7; do not infer CON grants it. Default hard-control DR is off. Record chained hard-control time and assess whether the optional prototype rule is justified after actual play.

Basic negative dispel removes slow/root/silence/mute/disarm/blind and Ledger Mark. Strong includes stun/hex/break. Basic positive dispel removes ordinary barriers/Anchor; it does not destroy wards, domes or geometry. Strong positive dispel is not an added universal mechanic. Source-bound reveal/grounding ends through source loss, range/geometry or expiry. Dispel does not rewind damage/displacement. Stun/hex prevent using a normal self-cleanse, even when that item could remove the tag in principle. Silence prevents Purge Touch but allows Null Bell; mute does the reverse. An external consenting Remedy Bind can remove hard disable.

## Protection and stacking

Spell block is one charge for hostile unit-target packets. AoE/ground/cone, tower basic attacks and environment bypass it. Control guard is one charge that strips the next stun/hex component only; accompanying damage or silence still resolves. If a packet is spell-blocked first, control guard is not spent. Reflection precedes spell block and cannot reflect a reflected packet again. Dome intercepts projectiles when crossing its boundary, not instant Counterpulse, Force Wave, melee or walking inside.

Use the strongest current general barrier, never sum pools; entering a stronger barrier replaces the pool rather than filling both. The universal pool cap is 40 percent of max HP. Brace Wall is the same pool family but takes damage only from the front 120-degree arc captured at activation; turning is allowed only through ordinary facing, no instant UI orientation exploit. Rear hits bypass its pool. Barrier absorption does not cancel the application of control or explicit interrupt. Damage reaching zero after mitigation/absorption is still positive combat contact only if post-mitigation barrier/HP damage was positive.

Ordinary armor and magical mitigation are separately capped at 60 percent against their respective types. No additive stacking of multiple resistance stages to bypass that cap. Major protection activation breaks concealment and uses the eight-second family lock. No ability grants blanket channel immunity or ten seconds of invulnerability.

## Basic attacks and tuning intent

Basic attack is an explicit A/target command, not automatic violence on neutral right-click. Apply one legal hit per server attack interval; attack speed cap 20 percent. Melee rechecks range at hit; ranged attacks launch a non-homing projectile toward observed target snapshot. An A-confirm on an observed unit starts a maintained attack order with range/sight checked before every windup; Stop, another action, life change or sight loss cancels it. Default attack windup is300 ms (both melee and ranged); the hit/launch must occur within the selected attack interval. Ranged basic projectiles move1200 cm/s toward the observed release snapshot, never hidden homing. No attack order silently reacquires a disappeared body. No default crit/evasion RNG in new combat. Preserve existing theft randomness separately. Physical weapon bases and derived formulas are in progression/data. Armor is expressed as armor points, MR as a fraction. Environmental damage/official service immunity are explicit categories, not hidden per-character exceptions.

Aim for ordinary evenly matched fights around 8–15 seconds after commitment, with escapes and ambushes producing variation. This is a playtest target, not a guaranteed TTK formula; compute actual baseline matchups and tune damage/resource costs together. Low-level utility timing should remain effective even if damage is insufficient to win a straight duel.

## Tick arithmetic

Damage zones divide their listed premitigation total into duration/tick_ms scheduled components, allocating any fixed-point remainder to the last tick. A body entering late receives only remaining scheduled components; it never receives a new full total per collision callback. Each zone/source/victim-life/tick ID applies once. Salve healing ticks once per second with a final remainder and stops remaining ticks after positive HP/barrier damage. Regeneration accumulates by authoritative elapsed time in fixed point, capped at current maximum. Do not grant a full extra tick from widget reopen, source overlap or frame-rate differences.
