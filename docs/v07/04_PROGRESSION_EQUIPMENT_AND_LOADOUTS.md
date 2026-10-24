# Character progression, equipment and preparation

## Natural investment and equal starts

Six attributes STR/DEX/CON/INT/WIS/CHA are investable. New presets use 8 each +3 primary +2 secondary, total 53. All kits' skills are book-learnable across classes. Preserve delivered economic classes/utilities and normalize budget only through an explicitly recorded migration. No primary-stat automatic growth. Starting class is not a permanent cast lock.

Natural attribute = starting + permanently allocated points, cap 16. Earn one allocation point per level after level one; no respec in v0.7. Gear and temporary effects change effective attributes but never meet natural spell gates. Learning may happen while ineligible; equipping AND casting revalidate. Examples INT9 plus +3 gear is effective12/natural9 and cannot equip Temporal Lock (natural INT13/WIS10). Requirements_any is OR across objects, AND inside an object.

Five slots contain learned actives of any domain, including Analyze, Survey, Parley, Heave or theft. One Circle-IV ability may occupy those five at most. No separate noncombat bar, contextual bypass, free old innate dash or duplicate variant. Basic movement/attack/ordinary service interaction remain basic controls. Circle is ability complexity; rank is personal mastery. No class gates on ordinary books.

## XP and mastery

Use cumulative XP for levels 1–15: 0,75,150,300,450,650,875,1125,1400,1700,2025,2375,2750,3150,3575. XP at cap can remain for telemetry but grants no further points. Existing meaningful bounded work/procurement/discovery rewards remain. No XP for craft, gathering, bank, buy/sell, book duplication or circular peer trades. Camp/boss/tower/ruin/PvP additions have explicit budgets. Killing ordinary residents grants no new reward.

Each earned level grants one mastery point as well as one attribute point. New learned skills start Rank 1. Rank 2 costs one point and level 3; Rank 3 costs an additional two and level 6. Require a preparation station and two-second hold for rank investment/loadout/attribute drafts; all proposed spend commits atomically once. Books teach only Rank 1, take a three-second quiet read and disappear on successful learning. Duplicate read rejects and keeps the item. No rank XP by repeated casting or duplicate-book grinding. Class starters begin Rank 1 and may train identically. Preserve audited legacy rank formulas until normalized through documented adapters; any legacy utility occupying a slot still follows cooldown/life legality.

Equipment may add at most one effective potency rank total, capped at effective rank 3. It affects only fields explicitly in potency_fields (damage/barrier/radius/anchor HP), never hard-control duration, cooldown, gates, missing knowledge, upgraded mechanic or ordinary mastery. Trained rank controls those other rank fields. Empty potency fields means no benefit for that spell. Scholar's Sigil applies to school `control`, not all skills. It may increase Temporal Lock radius slightly but never stun time.

## Secondary combat formulas

All are selected provisional defaults for new combat, not claims about earlier implementation:
| Stat | Formula / limit |
| --- | --- |
| HP | 600 +25*(effective CON−8) +18*(level−1) |
| MP | 180 +12*(effective INT−8) +6*(effective WIS−8) +5*(level−1) |
| Regeneration | HP1/s; MP2/s, plus explicit gear contribution |
| Weapon attack damage | Weapon base +2*max(0,effective STR−8); unarmed base42 |
| Attack interval | Default1200 ms; weapon override; at most20 percent faster |
| Base movement | 420 cm/s, positive bonuses cap 20 percent, strongest slow cap 60 percent |
| Base armor | 2+0.5*(effective CON−8), add armor; physical reduction min(0.60,A/(100+A)) with A>=0 |
| Base magic reduction | 0.15 plus explicit MR, cap 0.60 |
| Magical/heal/barrier skill magnitude | min(1.25,1+0.025*max(0,effective INT−8)); pure control has no magnitude multiplier |
| Barrier | Strongest current pool only, at most40 percent max HP |
| Capacity | 60+2*(natural STR−10)+eligible equipped capacity bonuses |

For listed healing/barrier skills use the same INT magnitude multiplier only when effect defines a numeric amount; consumables and item actives do not scale with attributes. Mastery chooses field base first, then INT multiplier for those skill amounts. Radius/cooldown/duration do not receive INT scaling. Keep attack range and cast-range bonuses bounded to one strongest gear contribution each; Conduit Staff +50 cm does not extend global/teleport pair distances, Heart range, service range or detectors. HP/MP reductions clamp current to new max without granting resources on re-equip. Increased maximum never fills the difference. No WIS wall vision, DEX cast-time bypass or CHA universal income multiplier.

## Equipment bindings and caps

Weapon, offhand, body, feet, charm A/B. A two-handed weapon reserves weapon+offhand. Bound gear remains a single carried instance in the same 24-slot grid, marked equipped; no extra storage slots. All carried gear weighs once. Primary gear bonuses cap +2 per piece/attribute, +3 per attribute and +6 across all attributes. Apply a deterministic allocation: binding order weapon, offhand, body, feet, charm A, charm B; within a piece STR,DEX,CON,INT,WIS,CHA. Grant each positive bonus up to remaining limits. Show inactive excess per item/stat before confirmation. Negative secondary tradeoffs always apply. Craftsmanship never increases primary bonuses. Example full kit with +8 requested primary points applies first six, not eight.

Gear passive benefits require equipment binding. Gear actives additionally require one of three quick references to that same ID. Consumable active requires quick reference but no equipment binding. A reusable unbound gadget gets no free bag activation. Learned abilities never become item references. Books use the quiet read action from inventory, not a combat quick-slot activation; a learned skill still needs one of five slots.

## Preparation and no midfight backpack bypass

Viewing inventory is immediate and nonpausing. Binding/unbinding gear or quick references requires six seconds since positive damage dealt/taken and no live hostile windup/projectile owned by the contender, then a two-second stationary hold. Positive damage, movement, casting, item use, hold loss or death cancels. Cannot change references while stunned/hexed; mute prevents quick-item binding, silence prevents skill preparation. This commitment is not a combat dispel. Passive offensive contact (e.g. owned damage zone) refreshes combat quiet. Unknown hidden opponents do not become an oracle: only own actual combat contact/owned pending actions affect eligibility.

Learned-skill/attribute/rank changes additionally require a preparation station within120 cm and sight. Cooldowns remain on ability identity/contender through changes and death; replacement cannot reset. Quick family cooldowns and instance cooldowns both apply. Equipped/reserved items cannot be sold, dropped or consumed as recipe inputs; explicitly unbind first. A draft is rejected if resulting count/weight exceeds limits, including removal of Merchant Harness/Weighted Satchel. Preparation cannot store overweight state or refill HP/MP. These rules apply to keyboard, drag menus, context F and direct requests alike.
