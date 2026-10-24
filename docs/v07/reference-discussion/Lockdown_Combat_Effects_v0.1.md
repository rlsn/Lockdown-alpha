# Project Lockdown — Combat Effects, Abilities & Items v0.1

**Status:** Design draft / playtest proposal  
**Date:** 2026-10-24  
**Scope:** Real-time combat rules, progression prerequisites, equipment, spell taxonomy, and initial content catalog.  
**Not in scope:** Final economy prices, visual effects, animation specifications, exact damage formulas, or complete skill trees.

## 0. Fixed context and goals

Lockdown is a 60–90-minute multiplayer stealth-to-chaos RPG/MOBA with six attributes (STR, DEX, CON, INT, WIS, CHA), right-click movement, fog of war, autonomous NPCs, and the Grand Heart as the sole win condition. Victory access can be achieved by siege, a Sovereign Charter, three keys, or a constructed artifact. All culminate in a contested ten-second claim.

The economy has reliable official shops, finite-stock/liquidity regional markets, short crafting chains, physical banks, risky cargo, and costs for services, equipment, and victory investments. Equipped gear, learned skills, and permanent progression persist across death; carried cargo, unique keys, and other designated contested objects can drop. Respawn delays escalate.

**Design promises:**

1. Control, counterplay, positioning, information, and item timing can defeat a raw level advantage.
2. Every powerful effect has a response window, an opportunity cost, a counter, or a positional weakness.
3. Skills are generally learnable by any class through books; advanced casting requires build commitment.
4. Low-tier counter-items remain available at official vendors, not wholly subject to market cornering.
5. Item tier, item craftsmanship, spell circle, and spell mastery are independent concepts.
6. Combat-only and utility skills compete for the same five active skill slots; only three items are quick-activatable.

All numerical values below are **first-playtest targets**, not settled tuning.

## 1. Character and equipment model

### 1.1 Attributes

Six primary attributes are the only investable character stats. Suggested starting template: all attributes 5, +3 to class primary and +2 to class secondary. Gain one freely allocatable point per level; proposed natural cap is 16 pending testing. Skills may scale with one or two attributes without having an exclusive class owner.

| Primary attribute | Typical combat scaling | Noncombat consequences |
| --- | --- | --- |
| STR | Physical impact, displacement strength, heavy weapons | Carrying and forced entry |
| DEX | Attack execution, mobility, evasion | Sneaking, theft, fine manipulation |
| CON | Health, recovery, resistance to sustained harm | Hazard endurance |
| INT | Magical potency, arcane constructs, mana | Engineering, crafting, magic research |
| WIS | Detection, precise control, dispel efficacy | Scouting, tracking, analysis |
| CHA | Deception, summons, binding/command effects | Trade, contracts, reputation |

**Derived secondary stats:** HP, MP, attack damage/speed, armor, magic resistance, movement speed, cast range, spell magnitude, barrier strength, regeneration, vision radius, stealth signature, carrying capacity. Items may affect primary attributes or secondary stats directly; they are not additional investable point pools.

### 1.2 Equipment and inventory

- Slots: weapon, offhand, body armor, footwear, charm A, charm B. Two-handed weapons occupy weapon + offhand.
- Five active skills, shared by combat and utility. Three quick-item slots, shared by consumables and activated equipment. Inventory can be large but is weight-limited.
- Equipped gear provides passive bonuses even if its active ability is not quick-slotted. Activating equipment requires an assigned quick-item slot.
- Individual gear pieces grant at most +2 to any primary attribute. Total equipped bonus is capped at +6 primary points, with no more than +3 in any one attribute; UI must clearly show inactive excess bonuses.
- Damage, armor, resistance, and movement bonuses must compete with carrying utility, stealth, vision, resource economy, and active effects. Avoid universally optimal equipment.

### 1.3 Item tier vs craftsmanship

| Axis | Levels | Meaning |
| --- | --- | --- |
| **Tier** | I Basic, II Specialized, III Exceptional, IV Singular | Item's identity, available effects, strategic rarity and acquisition difficulty |
| **Craftsmanship** | Standard, Fine, Masterwork | Quality of its physical implementation, not a new mechanic |

Craftsmanship improves the item's **base attack/armor/barrier number** by an illustrative +0% / +7% / +14% respectively. It does **not** increase primary attribute points, control durations, dispel category, number of charges, spell prerequisites, or the scope of a unique active. A Masterwork Tier-I sword may deal more ordinary attack damage than a Standard Tier-II utility weapon, while lacking its special ability. Recipes may upgrade craftsmanship in short chains; avoid randomized loot affixes for initial testing.

### 1.4 Spell circle vs mastery rank

| Axis | Levels | Meaning |
| --- | --- | --- |
| **Circle** | I, II, III, IV | Intrinsic complexity, unusual mechanical reach, prerequisite burden |
| **Trained rank** | 1, 2, 3 | Character's mastery of a particular learned ability |
| **Variant** | Named alternative | Different tactical behavior, not merely another damage multiplier |

- Circle I: accessible baseline effects. Circle II: specialization. Circle III: powerful positional/defensive effects. Circle IV: match-shaping capstones.
- Increasing a rank generally raises damage, healing, barrier strength, radius, charge economy, or modestly lowers cooldown. **Hard-control duration should change little**, e.g. 1.2 -> 1.35 -> 1.5 s, and never double as a routine rank upgrade.
- Books teach abilities, trained ranks, or explicit variants. Do not generate random-quality versions of the same spellbook. Discovering a rare Circle-IV book is exciting because of its mechanic, not a color-coded roll.
- Learning a spell is not the same as equipping or casting it: a character can own a discovered spell while its prerequisite remains unmet.
- Exactly **one Circle-IV ability may be equipped** in the five active slots at a time; owning more remains possible.

### 1.5 Attribute and class requirements

General design: **classes define affinity, not permanent prohibitions**. Class starts with two appropriate skills and may receive small affinity discounts or efficiency bonuses. Most later abilities are learnable through books regardless of class. A few future innate class passives may remain exclusive, but not the central combat counter-toolkit.

| Circle | Default gate examples | Other limitations |
| --- | --- | --- |
| I | None, or one attribute >=8 | Short cooldowns, modest MP |
| II | One attribute >=9–10 | More substantial MP and slot commitment |
| III | Primary >=11 and supporting >=8–9 | Visible cast, positional weakness, longer cooldown |
| IV | Primary >=13 and supporting >=10 | One capstone equipped, high MP, long cooldown and telegraph |

**Prerequisites use natural stats** (starting + permanently allocated) rather than ordinary gear, temporary buffs, consumables, or quality bonuses. Gear helps spell *performance*, not shortcutting the specialization investment needed to equip high-circle spells. Exception items that explicitly grant attunement could be designed later, separately and with hard limits.

**Ability-rank bonuses from equipment:** Certain rare items grant `+1 effective potency rank` to one named spell or school, capped at one such bonus total and at trained rank 3. This only improves the magnitude-side scaling (damage/heal/shield, bounded area); it never grants a missing ability, satisfies a prerequisite, adds an upgrade-only mechanic, or extends a hard-disable duration. This prevents a staff-plus-robe stack from granting an unearned capstone.

### 1.6 Anti-synergy and balance constraints

1. Same-family unique passive bonuses do not stack; use the strongest instance.
2. Major protections (e.g., phase immunity / projectile sanctuary / absolute barrier) have exclusive activation categories where needed, and their effects never create indefinite invulnerability.
3. Activating major protection breaks stealth. Beginning the Grand Heart claim breaks concealment and prevents new movement/teleport effects while channeling.
4. Defenses have bounded mitigation, provisionally a 60% combined reduction from ordinary armor/resistance before explicit shields; shields do not confer control immunity by default.
5. Spell blocks are charge-based and do not accumulate unlimited charges from multiple sources.
6. Ordinary movement speed bonuses are bounded; slows of the same category are not additive to immobility.
7. A spell's target, area, dispel type, protection type, and special exceptions must be explicit. Do not solve balance problems through hidden immunities.

## 2. Combat-effect engine rules

### 2.1 Resolution pipeline

1. **Validate:** alive/active, ability equipped, Circle-IV slot rule, natural prerequisites, silence/mute restrictions, sufficient resources, cooldown, legal target, maximum range, and relevant line of sight.
2. **Windup:** visible cast point where applicable. Moving or being hard-disabled may interrupt. A windup cancelled before release spends no full mana cost but imposes a brief recovery to prevent free feint spam.
3. **Release:** consume MP/charge and begin cooldown; spawn projectile, instant target effect, area anchor, or channel. Channeled actions pay costs on channel start.
4. **Interception:** apply spell-block charge, applicable protection/phase state, and projectile/terrain interactions before status or damage.
5. **Apply effects:** resolve forced movement, damage/healing, shields, statuses, terrain changes, and visibility changes in a documented deterministic order per ability.
6. **Maintain:** tick durations, barriers, area anchors, ownership and line-of-sight updates; end temporary states on expiry/destruction/interruption.
7. **Cleanup:** remove expired tags and recalculate secondary stats and vision. Log cause and mitigation for the combat replay/debugger.

### 2.2 Targeting and geometry

- Target forms: self, unit, ally/owned unit, point, line, cone, area, construct. The UI should distinguish each form.
- Targeted casts normally need vision at release. Ground-targeted area abilities can be cast into unseen territory if terrain/range permit; this is intentional counterplay to concealment.
- Smoke removes ordinary sight but need not erase projectile trajectories already released. Revealing a concealed target shows its current location/outline, **not necessarily its player identity**.
- High ground, buildings, walls, trees, bridges, and destructible objects govern vision and pathing. Displacement cannot move units through solid walls unless it explicitly has a phase/teleport tag.
- Root blocks voluntary movement/dashes/blinks; it does not prevent being forcibly pushed or pulled. Displacement interrupts movement-dependent channels including Heart capture.
- In the default free-for-all rules, offensive areas can affect all other contesting units in range; temporary social alliances do not automatically grant immunity. Owned summons and clearly designated friendly targets follow their ability's targeting rules. Attacking civilians in surveilled zones has law/guard consequences.

### 2.3 Status vocabulary

| Tag | Behavior | Basic dispel? | Strong dispel? | Principal counter |
| --- | --- | --- | --- | --- |
| Stun | No movement, attacks, skill casts, active items | No | Yes, externally/preemptively | Spell block, ally rescue, pre-buff |
| Silence | No active skills | Yes | Yes | Item dispel, active item |
| Mute | No active items | Yes | Yes | Innate skills, pre-buff |
| Root | No voluntary movement, dash or blink | Yes | Yes | Dispel, fight in place |
| Disarm | No basic attacks | Yes | Yes | Cast skills |
| Slow | Reduces movement | Yes | Yes | Mobility or dispel |
| Break | Disables designated passives | No | Yes | Active effects, disengage |
| Reveal | Detects concealed physical position; not identity | Removal depends on detector source | N/A | Break sight, destroy detector |
| Blind | Shrinks reliable sight/targeting | Yes | Yes | Area casts, detection |
| Hex | Cannot use most abilities/attacks; restricted movement | No | Yes | Spell block, ally strong dispel |
| Displacement | One-time push/pull/swap | Not retroactive | Not retroactive | Position, anchor protection, phase |

Dispel is an active *action* with its own cast conditions. A stunned player cannot trigger a normal cleanse item; an ally can apply a strong dispel, or the player can pre-buff before the stun. This distinction is fundamental to coordinated play.

**Status stacking:** Reapplications of the same tag take the stronger magnitude and/or longer remaining duration rather than adding full durations. Distinct tags can coexist (e.g. silence + root). Status resistance does not quietly nullify low-level utility; any resistance must be readable. Begin testing with 1–2 second normal stuns, 2–4 second soft disables, and visible cast/defense windows. Compare a no-DR build against a prototype that grants one brief hard-control protection after >4 seconds of cumulative hard control within 8 seconds. Select on playtest evidence, not assumption.

### 2.4 Damage, barriers, and counters

- Damage types: physical, magical, rare pure. Armor reduces physical; ward/magic resistance reduces magical; pure bypasses those resistances but is budgeted sparingly.
- Barriers consume damage before HP but do not prevent displacement/status unless explicitly tagged. Healing reduction and barrier bypass are specialized counters.
- Defensive counter types: basic dispel, strong dispel, one-charge spell block, projectile block, detection, disengagement, terrain destruction, interrupt, mobility denial.
- Cast-point and channel interruption should often matter more than a marginal DPS upgrade.
- Ordinary attack damage supports threat and finishing power without rendering active defenses irrelevant.

### 2.5 Death and objective interaction

- Learned abilities, natural points, and equipped gear persist on death. Carried trade commodities, unique keys, and marked contested objects drop; unbanked wealth is at risk under the economy specification.
- A Grand Heart claim takes ten uninterrupted seconds while staying in the claim zone. Stun, forced movement, death, leaving the zone, and explicitly tagged channel interrupts cancel it. Ordinary damage alone does not automatically cancel.
- Claiming breaks stealth and disallows free spell/item activation by the claimant during the channel. Predeployed defenses, constructs, decoys, and allied actions can help defend.
- Siege opens conventional access globally; Charter, keys, and artifact grant personal bypass mechanisms. Their activation reveals sufficient information to allow contest without always instantly identifying the claimant.

## 3. Initial ability catalog

Legend: `C` is Circle, `CD` is provisional cooldown in seconds, and stat gates refer to **natural** attributes. Durations are provisional at rank 1. Starting class skills can be acquired by others from books later; class merely provides earlier access/affinity.

### 3.1 Twelve class starters

| Ability | Starting class | C | Effect | CD | Counter / weakness |
| --- | --- | --- | --- | --- | --- |
| Anchor Bash | Vanguard | I | Short-range 1.2 s stun and minor impact damage | 12 | Short reach, spell block, kite |
| Brace Wall | Vanguard | I | Directional barrier against front-facing damage for 3 s | 24 | Flank, displacement, magic angle |
| Veil Step | Infiltrator | I | 5 s concealment; attacking/casting breaks it | 26 | Detection, area damage, surveillance |
| Hamstring | Infiltrator | I | Physical strike +40% slow for 2.5 s | 10 | Dispel, range denial |
| Arc Bolt | Arcanist | I | Reliable targeted magical strike; modest damage scaling | 7 | Spell block, magic resistance |
| Counterpulse | Arcanist | I | Interrupt current channel, silence for 1.3 s | 20 | Bait, spell block, long approach |
| Farwatch Ward | Seeker | I | Deploy a destructible vision anchor for 60 s | 24 | Counterward, destroy, route change |
| Wire Snare | Seeker | I | Placed trap, roots first triggering unit 1.6 s | 20 | Detection, dispel, alternate path |
| Ledger Mark | Broker | I | Track target's recent cargo route for 12 s, without exposing true identity | 30 | Remove mark, take alternate route |
| Surety Shell | Broker | I | Short personal barrier against one incoming burst | 32 | Small attacks, bait and wait |
| False Trail | Trickster | I | Deploy plausible mobile decoy/footsteps for 7 s | 24 | Careful targeting, detector |
| Smoke Veil | Trickster | I | Five-second smoke area blocks ordinary unit vision | 28 | Ground-target AoE, reposition, scout |

### 3.2 Twelve learnable universal abilities

| Ability | C / gate | Effect | CD | Counter / weakness |
| --- | --- | --- | --- | --- |
| Purge Touch | I / none | Basic dispel on self or nearby unit | 30 | Silence or stun prevents self-casting; interrupt the cast |
| Seer's Flare | I / WIS 8 | Briefly reveals concealed units in an area for 5 s | 24 | Leave area, obscure route |
| Ember Line | I / INT 8 | Ground line that damages and denies a narrow path for 3 s | 18 | Walk around, resistance |
| Grapple | I / STR 8 or DEX 8 | Pull self to valid anchor or drag a light target a short distance | 21 | Positioning, anchor destruction |
| Null Seal | II / INT 9 or CHA 10 | Mute targeted unit's active items for 1.8 s | 27 | Innate spells, spell block |
| Grounding Field | II / WIS 9 | Six-second area prevents blink/dash activation | 34 | Destroy anchor, walk out |
| Force Wave | II / STR 9 or INT 9 | Cone of pushback; interrupts channel if displaced | 24 | Flank, projectile shield doesn't stop wave |
| Guard Swap | II / WIS 9, CON 7 | Swap with ally/owned construct or short-move self to planted marker | 31 | Destroy marker, block release |
| Slipstream | II / DEX 10 | Fast short dash, cannot pass solid walls | 22 | Root, Grounding Field |
| Remedy Bind | II / WIS 9, CON 7 | Strong dispel on a nearby other unit; can preapply brief self-control shield | 44 | Bait cooldown, separation |
| Break Sigil | III / INT 11, WIS 9 | Suppress designated target passives for 4 s | 41 | Strong dispel, active tools |
| Arcane Prison | III / INT 11, WIS 9 | Create destructible movement-blocking ring for 3.5 s | 47 | Destroy anchor, displacement, phase |

Purge Touch is a **skill**, so silence (not mute) prevents self-casting. A separate item-based basic dispel is required to escape silence.

### 3.3 Four Circle-IV capstones

| Ability | Gate | Effect | CD | Explicit weakness |
| --- | --- | --- | --- | --- |
| Temporal Lock | INT 13, WIS 10 | Telegraphed area stun up to 1.8 s | 115 | Visible windup, spell protection, limited area |
| Sovereign Ward | CON 12, WIS 11 | Four-second dome that intercepts projectiles, not melee entry or forced movement | 105 | Walk inside, destroy emitter, wait |
| Grand Exchange | CHA 13, DEX 10 | Swap the positions of two valid visible units within local range | 95 | Target legality, spell shields, setup time |
| Gate Weave | INT 13, CHA 10 | Place two temporary connected travel gates up to 40 m apart, lasting 18 s | 120 | Destroy gates, guard exits, interrupt cast |

Each has a significant MP cost, a detectable windup, and the one-capstone-equipped limit. These are **alternatives**, not linear DPS upgrades.

### 3.4 Illustrative mastery scaling

| Skill | Rank 1 | Rank 2 | Rank 3 |
| --- | --- | --- | --- |
| Anchor Bash | Stun 1.2 s | Stun 1.35 s | Stun 1.5 s |
| Arc Bolt | Base damage | +15% base damage | +30% base damage |
| Farwatch Ward | 60 s duration | 70 s | 80 s |
| Smoke Veil | Radius 4 m | Radius 4.5 m | Radius 5 m |
| Remedy Bind | 44 s CD | 40 s | 36 s |

Magnitude bonuses from equipment may boost damage/barrier/radius modestly up to the rank-3 tuning point, not control durations or new rank perks.

## 4. Initial item catalog

Item tags: `P` means passive while equipped; `A` means active requiring a quick-item slot; `C` means consumable, also requiring a quick slot. Gear craftsmanship modifies only its base numerical weapon/armor/barrier strength as specified earlier. Tier is written `T`.

### 4.1 Weapons (6)

| Item | T | Bonus/effect | Tradeoff |
| --- | --- | --- | --- |
| Iron Halberd | I | Two-handed, STR +1; longer melee reach (P) | No offhand, slow attacks |
| Watchblade | II | DEX +1, WIS +1; brief trail mark on target after consecutive hits (P) | Less burst |
| Runic Staff | II | INT +2; improves magical magnitude slightly (P) | Low physical pressure |
| Surveyor Bow | II | DEX +1, WIS +1; extended attack/observation range (P) | Vulnerable to close assault |
| Oath Mace | III | STR +1, CHA +1; heavy blows damage barriers efficiently (P) | Slow windup, noisy |
| Null Dagger | III | DEX +1, INT +1; next attack inflicts brief silence (A, 55 s CD) | Consumes item slot, short reach |

### 4.2 Armor (5)

| Item | T | Bonus/effect | Tradeoff |
| --- | --- | --- | --- |
| Bastion Plate | I | CON +2; high physical armor | -10% movement speed |
| Shade Mantle | II | DEX +1, WIS +1; reduced ordinary detection footprint | Weak armor, reveal still counters |
| Archivist Robe | II | INT +1, WIS +1; mana regeneration | Weak physical defense |
| Merchant Harness | I | CON +1, CHA +1; additional commodity carrying capacity | Low combat specialization |
| Reflective Mail | III | CON +1, INT +1; small magic resist (P), reflect one projectile spell (A, 65 s) | Quick-slot opportunity cost |

### 4.3 Footwear and charms (9)

| Item | Slot / T | Bonus/effect | Tradeoff |
| --- | --- | --- | --- |
| Pathfinder Boots | Boots / I | Movement speed on roads (P) | Less off-road benefit |
| Silent Soles | Boots / II | DEX +1, quieter travel (P) | No defense |
| Cliffwalkers | Boots / II | WIS +1, access defined scramble shortcuts (P) | No speed bonus |
| Anchor Greaves | Boots / III | CON +1; brief knockback resistance when braced (A) | Slower turning/bracing |
| Scholar's Sigil | Charm / III | INT +1; +1 potency-only effective rank to one school (P) | No hard-control/rank-perk increase; unique family cap |
| Broker's Seal | Charm / II | CHA +1; reduced select contract fees (P) | Economic slot allocation |
| Warding Eye | Charm / II | WIS +1; increases detection radius (P) | Reveals presence to skilled observers |
| Iron Oath | Charm / III | STR +1; improves directional guard/barrier strength (P) | No offensive utility |
| Weighted Satchel | Charm / I | More carrying capacity (P) | Competes with combat charms |

### 4.4 Quick-use and consumable items (14)

| Item | T / type | Effect | Use limitation / counter |
| --- | --- | --- | --- |
| Null Bell | I / C | Basic dispel of silence/root/slow | Cannot use while stunned or muted |
| Smoke Flask | I / C | Five-second vision-obscuring cloud | Area-target spells still work |
| Seer's Dust | I / C | Reveal invisible/concealed units locally for 6 s | Doesn't expose true player identity |
| Healing Salve | I / C | Heal over several seconds | Damaging interruption |
| Mana Draught | I / C | Restore MP with short consumption cast | Vulnerable during use |
| Ward Totem | I / C | Temporary destructible vision source | Counterward/destruction |
| Recall Token | I / C | Channel return to selected service waypoint/bank | Interrupted by control/forced movement |
| Snare Mine | II / C | Placed 1.5-second root trap | Detection, avoidance, dispel |
| Counterglyph | II / C | Brief targeted item mute (less potent than trained Null Seal) | Counterspells, positioning |
| Mirror Sigil | II / C | Preapply single-charge targeted spell block for short duration | Bait with cheap cast |
| Purity Charm | II / C | Preapply short protection against next hard-disable, not usable once stunned | Timing, bait |
| Aegis Flask | II / C | Short barrier against burst damage | No protection from control/displacement |
| Grappling Chain | II / A | Pull self toward a valid terrain anchor (38 s CD) | Root, terrain constraints |
| Blink Charm | III / A | Blink 7 m; disabled for 3 s after taking direct hostile damage (55 s CD) | Catch early, root, grounding |

All 14 items compete for the same **three quick-item slots** when relevant. C-type items disappear on use and are item sinks. A-type equipment remains but occupies a quick slot to activate. Exact supply, restock and prices belong to the economy balance pass.

## 5. Counterplay examples and exploit tests

### 5.1 Underleveled objective denial

Level-8 Seeker versus level-14 tower-siege fighter near the Heart: Farwatch Ward detects an approach, Wire Snare constrains the entrance, Smoke Flask hides the defender, Force Wave interrupts the ten-second claim. The Seeker need not win a DPS duel to decide the objective; the fighter can still counter by clearing the ward, using detection, or baiting Force Wave.

### 5.2 Equipment-assisted INT stacking

An INT-9 Infiltrator equips Runic Staff (+2 INT), Archivist Robe (+1 INT), and Scholar's Sigil (+1 INT). Equipment effective INT is bounded by the +3 per-attribute cap; **natural INT remains 9**. The build casts existing low-circle spells more effectively but does not unlock Temporal Lock's natural INT-13 prerequisite. There is no equip-learn-unequip exploit.

### 5.3 Concealment + defense + capture

Veil Step ends as the Heart claim begins; using Sovereign Ward or a major defensive active breaks concealment. The ward's four seconds may protect part of the ten-second channel, but cannot grant full invulnerability. Opponents can enter the dome, displace the claimant, or time interrupts.

### 5.4 Dispel semantics

Silence prevents a character casting Purge Touch but not activating a Null Bell. Mute prevents Null Bell but not a learned Purge Touch. Stun prevents both; a nearby ally's Remedy Bind or previously activated Purity Charm is needed. This is intentional differentiated counterplay, not an inconsistency.

### 5.5 Economy and the combat toolkit

Official shops always carry a rotating but reliably replenished supply of basic tools: dispel, smoke, detection, ward, healing/mana. Regional markets and workshops supply specialized counters, traps, mobility, and exceptional gear. Merchant crafting should produce deliberate functional variants (e.g. longer smoke vs wider smoke) with a material/cost tradeoff, not random loot inflation. Spending on utility items is a real competing allocation against Charters, cargo, gear, and banking.

## 6. Implementation-facing data contract (conceptual)

An ability definition should declare:

```yaml
id: arcane_prison
circle: 3
school: arcane_control
targeting: ground_area
requirements:
  natural_INT: 11
  natural_WIS: 9
slot_cost: 1
cast:
  windup_s: 0.8
  cooldown_s: 47
  mana_cost: tuning.arcane_prison_mana
  requires_line_of_sight: false
effect:
  tags: [construct, movement_block, destructible_anchor]
  lifetime_s: 3.5
  radius_m: tuning.arcane_prison_radius
  anchor_hp: tuning.arcane_prison_anchor_hp
counters: [anchor_damage, displacement, phase]
rank_scaling: [anchor_hp, radius]
potency_rank_bonus_affects: [anchor_hp, radius]
dispel_class: none
```

Item definitions need separate fields for `tier`, `craftsmanship`, `equipment_slot`, `primary_stats`, `secondary_stats`, `unique_passive_family`, `active_ability_id`, `quick_slot_required`, `tradable`, `drops_on_death`, and `consumed_on_use`. Effect application should use explicit status tags, duration rules, line-of-sight rules, immunity/dispel tags, source ownership, and stacking family identifiers.

## 7. Minimum prototype and balance measurements

**First playable content slice:** 6 classes x 2 starter skills, 12 universal books, 4 rare capstones, 6 weapons, 5 body armors, 4 footwear options, 5 charms and 14 quick-use items. Not all content needs final animations or art.

Implement in this order:

1. Targeting and cast pipeline, range/line of sight, spell block, silence/mute/root/stun, basic dispel.
2. Reliable vision/fog/smoke and high-ground interactions, then forced movement and channels.
3. Skill/equipment slots, stat requirements, item tier/quality, spell circle/mastery, and rank boosts.
4. Most starters and cheap counter-items, then more specialized universal spells and capstones.
5. Contested ten-second Heart-capture scenario, merchant/cargo interception scenario, and key-carrier ambush.

Balance against explicit questions:

- Can a lower-level controller plausibly stop a Heart claim with preparation, without guaranteed victory in a duel?
- Do two different three-item loadouts lead to genuinely different answers to the same threat?
- Can players identify what disabled them and which actions would have countered it?
- Are defensive spell chains finite and does stealth break correctly when claiming an objective?
- Does an exceptional item offer distinctive utility rather than unconditional superiority to standard gear?
- Can a character learn high-circle spells through sustained attribute investment without starting as the corresponding class?
- Can players who focus on commerce continue to progress and protect themselves without becoming optimal combat specialists?

## 8. Outstanding design decisions

1. Final number of attribute points per match and highest natural attribute threshold.
2. Whether **all** class starter abilities eventually appear in books, or a small handful of signature passives remain class-unique (recommended: only signature passives).
3. Whether anti-chain-stun protection activates after cumulative hard control, based on tests of frustration versus coordinated control tactics.
4. Whether equipment stat bonuses beyond +3 to one attribute become inactive or instead receive diminishing returns (UI transparency is critical either way).
5. Whether advanced spell variants are mutually exclusive per spell or can be equipped as separate copies at additional slot cost (recommended: mutually exclusive within one spell).
6. Whether the player always has unrestricted access to inventory outside active combat, or item-loadout changes require an out-of-combat delay or safe service point (recommended: short out-of-combat delay; no midfight swap exploit).

