# Equipment and tactical item catalog

40 definitions: the original 34, five explicit assembly additions, and one contestable tower relic. Books and economy/objective materials are separate wrapper definitions. Every active shares three quick slots with consumables. An equipment active requires both equipment binding and quick reference; a consumable requires the quick reference. Items cost no MP; mute and cooldowns still apply. Item casts default to 250 ms windup and target sight at release; explicit channels override that. Unit-target hostile item effects use spell block.

| ID / name | Tier / binding | Attributes / weight | Official price | Passive | Active / CD | Death |
| --- | --- | --- | --- | --- | --- | --- |
| iron_halberd / Iron Halberd | 1 / weapon | {'STR': 1} / 6 | 24 | {"weapon_damage": 50, "reach_cm": 280, "attack_interval_ms": 1450} | null / 0 s | retain_only_if_equipped |
| watchblade / Watchblade | 2 / weapon | {'DEX': 1, 'WIS': 1} / 3 | 50 | {"weapon_damage": 42, "reach_cm": 220, "trail_after_hits": 3, "trail_ms": 2000, "breakable": true} | null / 0 s | retain_only_if_equipped |
| runic_staff / Runic Staff | 2 / weapon | {'INT': 2} / 3 | 48 | {"weapon_damage": 30, "reach_cm": 220, "spell_percent": 5} | null / 0 s | retain_only_if_equipped |
| surveyor_bow / Surveyor Bow | 2 / weapon | {'DEX': 1, 'WIS': 1} / 3 | 50 | {"weapon_damage": 36, "attack_range_cm": 600, "own_body_sight_bonus_cm": 50} | null / 0 s | retain_only_if_equipped |
| oath_mace / Oath Mace | 3 / weapon | {'STR': 1, 'CHA': 1} / 5 | recipe / objective | {"weapon_damage": 48, "reach_cm": 220, "attack_interval_ms": 1450, "barrier_damage_percent": 50, "breakable": true} | null / 0 s | retain_only_if_equipped |
| null_dagger / Null Dagger | 3 / weapon | {'DEX': 1, 'INT': 1} / 2 | recipe / objective | {"weapon_damage": 38, "reach_cm": 200} | {"op": "arm_next_hit", "duration_ms": 4000, "silence_ms": 1300, "family": "null_weapon"} / 55 s | retain_only_if_equipped |
| bastion_plate / Bastion Plate | 1 / body | {'CON': 2} / 8 | 28 | {"armor": 18, "movement_percent": -10} | null / 0 s | retain_only_if_equipped |
| shade_mantle / Shade Mantle | 2 / body | {'DEX': 1, 'WIS': 1} / 3 | 45 | {"armor": 4, "footstep_audible_range_percent": -20, "breakable": true} | null / 0 s | retain_only_if_equipped |
| archivist_robe / Archivist Robe | 2 / body | {'INT': 1, 'WIS': 1} / 3 | 44 | {"armor": 3, "mp_regen_per_second": 0.5} | null / 0 s | retain_only_if_equipped |
| merchant_harness / Merchant Harness | 1 / body | {'CON': 1, 'CHA': 1} / 3 | 24 | {"armor": 4, "capacity_bonus": 10} | null / 0 s | retain_only_if_equipped |
| reflective_mail / Reflective Mail | 3 / body | {'CON': 1, 'INT': 1} / 6 | recipe / objective | {"armor": 8, "magic_resist": 0.08} | {"op": "reflect_next_projectile", "duration_ms": 2500, "charges": 1, "family": "major_protection"} / 65 s | retain_only_if_equipped |
| pathfinder_boots / Pathfinder Boots | 1 / feet | {} / 2 | 18 | {"road_movement_percent": 12} | null / 0 s | retain_only_if_equipped |
| silent_soles / Silent Soles | 2 / feet | {'DEX': 1} / 2 | 30 | {"footstep_audible_range_percent": -20, "breakable": true} | null / 0 s | retain_only_if_equipped |
| cliffwalkers / Cliffwalkers | 2 / feet | {'WIS': 1} / 3 | 32 | {"authored_scramble_links": true} | null / 0 s | retain_only_if_equipped |
| anchor_greaves / Anchor Greaves | 3 / feet | {'CON': 1} / 4 | recipe / objective | {"movement_percent": -5} | {"op": "anchor", "duration_ms": 1800, "self_slow_percent": 30, "family": "major_protection"} / 45 s | retain_only_if_equipped |
| scholars_sigil / Scholar’s Sigil | 3 / charm | {'INT': 1} / 1 | recipe / objective | {"potency_rank": 1, "selected_school": "control", "unique_family": "potency_rank"} | null / 0 s | retain_only_if_equipped |
| brokers_seal / Broker’s Seal | 2 / charm | {'CHA': 1} / 1 | 30 | {"discount_gold": 1, "only_fees": ["equipment_assembly", "quality_upgrade"], "min_fee": 1, "unique_family": "fee_discount"} | null / 0 s | retain_only_if_equipped |
| warding_eye / Warding Eye | 2 / charm | {'WIS': 1} / 1 | 30 | {"detector": true, "radius_cm": 250, "line_of_sight": true, "breakable": true} | null / 0 s | retain_only_if_equipped |
| iron_oath / Iron Oath | 3 / charm | {'STR': 1} / 2 | recipe / objective | {"directional_barrier_percent": 10, "unique_family": "directional_guard", "breakable": true} | null / 0 s | retain_only_if_equipped |
| weighted_satchel / Weighted Satchel | 1 / charm | {} / 2 | 16 | {"capacity_bonus": 10, "unique_family": "satchel"} | null / 0 s | retain_only_if_equipped |
| null_bell / Null Bell | 1 / bag consumable | {} / 1 | 8 | {} | {"op": "basic_dispel_self", "family": "cleanse"} / 12 s | drop |
| smoke_flask / Smoke Flask | 1 / bag consumable | {} / 1 | 8 | {} | {"op": "smoke", "range_cm": 450, "radius_cm": 300, "duration_ms": 5000, "family": "smoke"} / 12 s | drop |
| seers_dust / Seer’s Dust | 1 / bag consumable | {} / 1 | 8 | {} | {"op": "detector", "radius_cm": 300, "duration_ms": 6000, "line_of_sight": true, "family": "detection"} / 12 s | drop |
| healing_salve / Healing Salve | 1 / bag consumable | {} / 1 | 6 | {} | {"op": "heal_over_time", "total": 160, "duration_ms": 8000, "cancel_on_positive_damage": true, "family": "recovery"} / 15 s | drop |
| mana_draught / Mana Draught | 1 / bag consumable | {} / 1 | 6 | {} | {"op": "restore_mp", "amount": 80, "channel_ms": 1500, "cancel_on_positive_damage": true, "family": "mana"} / 15 s | drop |
| ward_totem / Ward Totem | 1 / bag consumable | {} / 1 | 8 | {} | {"op": "spawn_ward", "range_cm": 350, "vision_cm": 560, "hp": 20, "duration_ms": 60000, "owner_limit": 2, "family": "ward"} / 12 s | drop |
| recall_token / Recall Token | 1 / bag consumable | {} / 1 | 10 | {} | {"op": "recall", "channel_ms": 4000, "valid_destinations": ["CityBank", "WestTownService", "EastTownService"], "cancel_on_positive_damage": true, "forbid_with_objective_cargo": true, "family": "travel"} / 20 s | drop |
| snare_mine / Snare Mine | 2 / bag consumable | {} / 1 | 14 | {} | {"op": "spawn_trap", "range_cm": 350, "hp": 20, "arm_ms": 1000, "duration_ms": 45000, "root_ms": 1500, "radius_cm": 100, "owner_limit": 2, "family": "trap"} / 15 s | drop |
| counterglyph / Counterglyph | 2 / bag consumable | {} / 1 | 18 | {} | {"op": "targeted_mute", "range_cm": 450, "duration_ms": 1200, "family": "mute"} / 20 s | drop |
| mirror_sigil / Mirror Sigil | 2 / bag consumable | {} / 1 | 16 | {} | {"op": "spell_block", "duration_ms": 3000, "charges": 1, "family": "major_protection"} / 20 s | drop |
| purity_charm / Purity Charm | 2 / bag consumable | {} / 1 | 16 | {} | {"op": "control_guard", "duration_ms": 1500, "charges": 1, "family": "major_protection"} / 20 s | drop |
| aegis_flask / Aegis Flask | 2 / bag consumable | {} / 1 | 14 | {} | {"op": "barrier", "amount": 160, "duration_ms": 3000, "family": "major_protection"} / 20 s | drop |
| grappling_chain / Grappling Chain | 2 / charm | {} / 2 | 35 | {} | {"op": "terrain_grapple", "range_cm": 500, "distance_cm": 500, "family": "grapple"} / 38 s | retain_only_if_equipped |
| blink_charm / Blink Charm | 3 / charm | {} / 1 | recipe / objective | {} | {"op": "blink", "distance_cm": 700, "positive_direct_damage_lock_ms": 3000, "family": "blink"} / 55 s | retain_only_if_equipped |
| simple_staff / Simple Staff | 1 / weapon | {} / 2 | 18 | {"weapon_damage": 32, "reach_cm": 220} | null / 0 s | retain_only_if_equipped |
| conduit_staff / Conduit Staff | 3 / weapon | {'INT': 1} / 3 | recipe / objective | {"weapon_damage": 34, "cast_range_bonus_cm": 50, "spell_percent": 5} | null / 0 s | retain_only_if_equipped |
| null_staff / Null Staff | 3 / weapon | {'WIS': 1} / 3 | recipe / objective | {"weapon_damage": 34, "reach_cm": 220} | {"op": "basic_dispel", "range_cm": 450, "polarity": "self-negative;other-positive", "family": "cleanse"} / 35 s | retain_only_if_equipped |
| rift_staff / Rift Staff | 3 / weapon | {'INT': 1, 'DEX': 1} / 3 | recipe / objective | {"weapon_damage": 32, "reach_cm": 220} | {"op": "blink", "distance_cm": 400, "positive_direct_damage_lock_ms": 3000, "family": "blink"} / 45 s | retain_only_if_equipped |
| harpoon_halberd / Harpoon Halberd | 3 / weapon | {'STR': 1} / 7 | recipe / objective | {"weapon_damage": 50, "reach_cm": 280, "attack_interval_ms": 1450} | {"op": "grapple", "range_cm": 500, "self_move_cm": 500, "target_pull_cm": 180, "family": "grapple"} / 38 s | retain_only_if_equipped |
| tower_relic / Tower Relic | 4 / charm | {'WIS': 1} / 2 | recipe / objective | {"armor": 4} | {"op": "barrier", "amount": 220, "duration_ms": 3000, "family": "major_protection"} / 60 s | always_drop |

## Consistent item semantics

Null Dagger's armed attack consumes the buff at next legal attack hit, not an aborted animation; cooldown and item cost commit when arming. The hit's silence can be spell-blocked independently of ordinary attack damage. Buff expires on owner death/unequip. Reflective Mail reverses one incoming projectile spell only; it does not reflect reflected projectiles, AoE, melee or pure environmental damage. Reflect at most once, using original source coefficients, and apply normal victim defenses on return.

Blink Charm and Rift Staff share the blink cooldown family: successful use locks every family member until that use's full cooldown endpoint. Positive direct hostile HP OR barrier damage locks use for three seconds; DoT, self-damage and environmental damage do not trigger this particular lock. Root and grounding always block it. Select a valid endpoint at most the listed distance; never clamp through solid geometry to land behind an invalid wall. Shorter valid endpoint selected by the user is legal.

Grappling Chain and Harpoon Halberd share grapple cooldown; all cleanse items share cleanse cooldown, all smoke items share smoke cooldown, etc. A transferred item retains its instance endpoint; new owner's family endpoint is the maximum of their existing endpoint and the transferred instance endpoint. Trading, death, sorting, assembly, quality upgrades and rebinding cannot reset cooldown or damage lock. Consumable family cooldown remains on the contender after the instance is destroyed.

Major protections share an eight-second activation lock, including Mirror Sigil, Purity Charm, Aegis Flask, Reflective Mail, Anchor Greaves and Tower Relic. Only one spell-block/control-guard charge can exist at once in each family; strongest remaining effect replaces, never adds. Sovereign Ward also starts the major-protection lock. Brace Wall and Surety Shell are barriers, not immunity; barriers do not sum and obey the universal barrier cap. This deliberately leaves low-tier dispels available rather than locking all utility after a defense.

Only numeric weapon damage, flat armor, or item-created barrier amount receives craftsmanship multipliers. Movement, attributes, passive percentages, cooldowns, active control duration, charges, mana restoration, healing consumables and potency rank do not. Round modified eligible values down. Tower Relic has fixed Standard quality and always drops even equipped. It is a tactical barrier, not access to the Heart or an invincibility reward.

Road bonuses and footstep reductions use authored road tags and audible ranges; no generic WIS-vs-DEX stealth roll is added. Cliffwalkers permit only authored scramble navlinks and respect root/grounding; they do not grant arbitrary cliff flight. Observation bonuses never pass geometry and do not expose private stats. Break suppresses only passives explicitly tagged breakable, not fundamental equipment armor, attributes or capacity.
