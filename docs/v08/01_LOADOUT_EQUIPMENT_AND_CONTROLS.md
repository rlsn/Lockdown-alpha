# Loadout, activation and preparation

## Six actions plus four accessories

Each character knows an unbounded library of learned actives but equips at most six. Every combat, magic, utility and profession skill competes here. At most one Circle-IV capstone. A learned action must be known, meet natural prerequisites and be equipped at release. One selected class trait is intrinsic; no learned skill provides an always-on passive just by knowing/equipping it. Timed concealment, barriers, wards and pulse buffs created by an actual cast are active effects, not passive learning.

| Binding | Permitted contents | Passive operation | Activation |
|---|---|---|---|
| Head, torso, legs, feet, gloves | Matching body item | While equipped | Only via one of shared six action slots if it grants an active |
| Main hand | One main weapon | Sets basic attack plus modifiers | Same six-slot rule |
| Off hand | One shield/focus/offhand | Modifiers/armor | Same six-slot rule |
| Accessory1–4 | Reusable accessory, artifact, one consumable instance | While equipped, if specified | This slot's one button, if it has an active |
| Backpack | Unbound owned items/books/materials | None | No combat activation |

A two-handed main weapon reserves off hand. No dual-wield damage system is introduced; Parrying Dagger grants its offhand stats/action without a second attack. Unarmed attacks remain universal base42 if main hand empty. Offhand alone is never an implicit weapon. No main-hand item in off hand. One instance cannot occupy multiple accessories, and no accessory may also occupy the shared action bar. Body active action is `source_instance_id + active_definition_id`; at most one per source; unbinding source atomically removes that slot action. Learned and gear-sourced counterparts sharing an authored family use common cooldown; cannot equip duplicate action identity/family to multiply casts. A passive-only accessory has an intentionally empty activation button. A hybrid accessory may provide both its listed passive and its one active.

Maximum independently equipped active actions = six shared slots + four accessories = ten. Basic attack, movement, Stop, inventory and authorized world/service interactions are ordinary controls, not extra combat abilities. Objectives/reading books remain quiet noncombat interactions. No weapon-action submenu, alternate-click second spell, consumable wheel or contextual cast may bypass these ten. Guard Swap's two phases occupy the same slot and common identity.

Examples: six learned skills plus four active accessories = ten; five learned skills plus Reflective Mail action plus four active accessories = ten; six learned skills plus four passive accessories = six. Equipping Reflective Mail without allocating its action still gives armor/MR but no reflection button. Four passive accessories cannot secretly activate inventory smoke bombs.

## Quiet preparation and instance rules

Preserve six-second combat quiet plus stationary two-second commit for changing body/accessory bindings; learned/equipment-sourced bar edits additionally need a preparation station within120 cm and sight. One atomic draft can replace source gear and corresponding actions; validate projected rules, not intermediate half-loadout. Skill/rank/attribute edits use station and existing point/gate costs. Movement, casting, item use, positive damage, pending own hostile windup/projectile, hard disable, focus loss or death cancels preparation. Owned persistent hostile damage updates quiet when it actually hits. No hidden-enemy proximity check. Inspecting inventory never pauses or commits.

Mute blocks accessory binding and allocating a body item action; Silence blocks learned-skill preparation. Combined bar changes require all relevant permissions. Silence alone does not block purely institutional recipe use. An enduring surface/ward is not a free item action after source removal: its effect uses snapshot attribution and explicitly documented lifetime, while binding/source changes stay quiet-gated.

A consumable accessory binding holds exactly one instance. Consuming it empties that binding; no auto-refill from backpack, world pickup or same-definition stack. Any future stacking needs a separate reviewed contract. Consumables drop on death even while equipped; ordinary reusable accessories/body gear retain if equipped. Objective-designated relics drop regardless of slot. Unbound spare gear drops. All11 possible equipped instances weigh/count once within24. Source unbind/sale/recipe/trade requires quiet commit first, preserving all cooldown endpoints and removing references atomically. Equipped consumable can be spent by its authorized action, but cannot be a recipe input/trade item while bound.

Instance, owner-family and learned-identity cooldowns persist through death, trade, dropping and rebinding. Check maximum applicable endpoint. Family cooldown length is listed action cooldown for same-family items, plus major_protection8s cross-family protection lock; use both. A replacement item cannot reset an owner's family lock; a transferred item cannot reset its instance lock. Passive proc readiness follows same owner-family+instance rule. No refill from max HP/MP gear changes: clamp decreases, never fill increases. Respawn full HP/MP is the existing explicit downtime rule.

## Attribute/progression budget

Retain v0.7 HP/MP/armor/level/XP/natural gates and mastery formulas in data/v08_rules.json. Gear primary bonuses: max+2 per item per attribute,+3 per attribute,+6 total over all eleven bindings. Deterministic cap allocation order is head, torso, legs, feet, gloves, main_hand, off_hand, accessory1–4; within item STR, DEX, CON, INT, WIS, CHA. Show suppressed excess before commit. One two-hand instance evaluated only at main hand. Negative secondary penalties always apply. No natural gate from gear, aura or temporary effects. Starting item/raw stat budgets are not inflated to fill new slots.

Trained rank1–3 selects normal bases; gear potency+1 maximum within cap3 on whitelisted fields only. Never boosts hard-control duration, cooldown, natural gate, knowledge or rank-unlocked mechanics. Body item active is not trained through learned-skill mastery and costs0MP; its listed flat item numbers and Mute rule apply despite appearing in six-slot bar. Signature learned skills cost MP normally. Attribute labels/proficiency flavor do not add undeclared bonuses.
