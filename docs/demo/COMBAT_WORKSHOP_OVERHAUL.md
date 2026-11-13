# Phase 4 — Combat & Rule Workshop Overhaul

**Status: implemented and build/rules verified; gameplay balance is not established.** This report distinguishes runtime/content facts, automated correctness tests, arithmetic, and design judgments. The added tests are rule-level fixtures, not complete PvP matches or evidence of player fun.

## A. Old and new workshop

| | Before Phase 4 | After Phase 4 |
|---|---:|---:|
| Bases | 18 | 18 |
| Mods | 12 | 15 |
| Connectors | 12 | 16 |
| Total | 42 | 49 |
| Common level-up pool | 39 | 46 |
| Scarce field-reward pool | 3 | 3 |

No components were removed. Seven were added: M13–M15 and C13–C16. B04 retained its identity but changed its lifesteal basis; B13–B16 retained stable IDs and icons while becoming active-skill rules. Thus 12 components were new or materially reworked; 11 of those 12 now respond to an active, dash, or an active-result event. The remaining one, B04, is intentionally still a basic-attack rule.

The rework addresses three structural issues: too many useful effects were self/basic-attack passives; mods often changed only reach/cooldown rather than the shape or timing of a skill; and connectors mostly added a proc rather than changing resource flow or a later action. The new rules add return/pierce/echo behavior, alternating-active setup, per-release cooldown/mana feedback, and a distinct active-hit shield trigger. Existing high-risk movement semantics and victory rules were not changed.

The exact native definitions remain authoritative in `SourceData/Demo/content.json`; player names and card copy are in its `display` rows. The complete running values and installed-host tables are maintained in `docs/demo/CONTENT_AND_BALANCE.md` §4.2 and §4.4. The tables in Report B enumerate the entire 49-piece directory; where existing operations have several per-host branches, those branches are stated separately.

## B. Complete B/M/C directory

`G(L)` scaling, damage/mitigation order, global status durations, and rounding remain those in `CONTENT_AND_BALANCE.md` §4.1/§4.5. Unless called out below, a component consumes its ordinary component instance/installation opportunity and has no separate MP or HP cost. “Native direct” means the host active's own direct ability event; reaction damage, burn ticks, added component damage, and basic attacks are not silently reclassified as native hits. Derived M13–M15 hits share the release `EffectId`, cannot recursively derive more hits, and count only against once-per-release component budgets.

### Bases (18)

| ID / display | Exact rule and legal use |
|---|---|
| B01 `component_fist_fire` 拳头冒火 | Each basic primary hit applies 1 burn stack. Basic-hit only. |
| B02 `component_tab_ledger` 揍一下记一笔 | Each valid basic/ability damage hit records 1 entry. At 5 entries, cash out at 10 + 0.08 AP magical damage per entry; clear unlocked entries after 6 s without a new entry. |
| B03 `component_spiky_hide` 打我也硌手 | After positive damage (including shield absorption) from an enemy basic primary hit, retaliate against its visible attacker within 600 cm for 10 + 1% max HP magical damage; crit multiplier 1, no interval. Not a basic attack. |
| B04 `component_punch_heal` 揍人续命 | Heal 12% of actual HP loss attributable to the basic's own damage package. Its share is own raw package / (own + merged empower packages); S11/S17/B12/C09 empower damage is excluded. Crit remains legal; shield absorption is not HP lost. |
| B05 `component_dash_bump` 撞你个趔趄 | D or active-dash real contact adds 30 + 0.25 AD + 0.25 AP physical damage once per enemy per dash. |
| B06 `component_third_punch` 第三拳滚蛋 | Every third basic primary hit pushes its target 120 cm over 0.12 s, no damage; the count is shared across targets and clears on death. |
| B07 `component_kick_down` 逮住就往狠了打 | +12% damage if, before the hit, the target was slowed, hard-controlled, or knocked back; joins the shared additive damage-amplification pool. Opening hosts: S01–S06, S08–S09, S12, S20, S22. |
| B08 `component_breach_fist` 越揍越不经打 | Basic primary hit applies 1 breach stack; global breach is −3 armor and magic resistance per stack, max 5, 4 s. |
| B09 `component_hard_skin` 挨揍起硬皮 | After positive incoming damage, attempt a 45 + 3.5% max-HP shield for 3 s; successful grant has a 10 s interval; a weaker shield neither replaces nor refreshes. |
| B10 `component_fist_water` 拳头甩脏水 | Basic primary hit applies wet for 4 s. |
| B11 `component_fist_shock` 拳头漏电 | Basic primary hit applies electrified for 4 s. Opening hosts: S07–S09, S20. |
| B12 `component_dash_punch` 蹿过去抡一拳 | On a voluntary dash's natural completion, empower the next basic for 3 s with +25 + 0.25 AD physical damage, one charge; same-source empower replaces rather than stacks. |
| B13 `component_catch_breath` 喘口气接着干 | First damaging native direct hit of a release returns 30% of that release's actually paid mana (after M06), once per `EffectId`; a zero-cost release returns zero. |
| B14 `component_long_arm` 离远点才好抽 | Native direct active damage gets +20% when caster/target foot distance is at least 550 cm; active damage only. |
| B15 `component_seeing_red` 一招接一招 | A different Q/W/E released within 4 s of the preceding active locks +20% native direct damage for that release. Every active release updates the previous-active record; D does not. |
| B16 `component_thick_hide` 打完就缩脖 | Positive-damage native direct active hit attempts a 40 + 3% max-HP shield for 3 s; successful grant has an 8 s interval; weaker shields do not replace/refresh. Basic hits do not trigger it. |
| B17 `component_held_shot` 憋着一发狠的 | An active ready and unused for 2 s gets +20% native direct damage on its next release; the wait restarts after release. Its opening eligibility list is S01–S10, S12/S13, S18, and S20–S22. |
| B18 `component_dirty_trick` 招里掺点脏的 | Opening eligibility is S01–S10, S12/S13, S16, and S18–S22. Bind one eligible learned damage active and fire/water/electric. Each native direct hit from that active adds 1 burn stack or wet/electrified for 4 s. Rest can rebind; forgetting the active suspends it rather than silently rebinding. |

### Mods (15; one installation each)

| ID / display | Legal hosts | Exact behavior, tradeoff, and tactical purpose |
|---|---|---|
| M01 `component_crowd_hit` 一群人一起挨 | S01–S06, S21; B02/B03/C08 | Unit spread: 60° and 70% damage. Projectile spread: 180 cm and 70%. Ledger spread: 220 cm and 70%. Counter spread: 250 cm and 80%. Trades single-target efficiency for multi-target pressure. |
| M02 `component_settle_later` 等会儿一起算 | B02/B03/C08 | Ledger cash-out delayed 2 s, 130% damage, 800 cm target bound; counter delayed 0.6 s, 130% damage, 180 cm radius. Adds a response window. |
| M03 `component_long_kick` 一脚踹出老远 | S01/S09, B06 | Knockback distance ×1.6; associated damage ×0.8. Positional control for lower immediate damage. |
| M04 `component_hold_down` 按住多揍一会 | S02–S05, S15 | Hard control duration ×1.25 and cooldown ×1.25. Longer setup, slower repeat. |
| M05 `component_reach_out` 站远点照样抽你 | S01–S06, S08–S10, S18–S21, S22; B03/C08 | Active range +200 cm up to a 1000 cm cap, active damage ×0.8; counter acquisition range +200 cm, counter damage ×0.8. It can give no range at the cap while retaining the damage penalty; no new install rejection was added. |
| M06 `component_again_now` 再来一下别喘 | All 22 actives | Cooldown ×0.8, paid mana ×1.25. Simple tempo-for-resource conversion. |
| M07 `component_one_slam` 合一块砸你 | S08/S10/S20/S22 | Cone: delay 0.6 s, angle 35°, damage ×1.15. Zone: one blast after 0.9 s, radius ×0.75 and summed pulse damage ×1.1. Chain: compressed damage ×0.8. Trades distributed timing/coverage for a concentrated punish. |
| M08 `component_ram_through` 闷头撞到底 | S11–S13 and D | Dash distance ×1.5, contact damage ×1.3, cooldown ×1.25. Longer commitment and exposure. |
| M09 `component_quick_hop` 蹿两步就回头 | S11–S13 and D | Dash distance ×0.7, cooldown ×0.75, damage unchanged. Shorter escape/reposition cycle. |
| M10 `component_big_tab` 攒够一顿狠的 | B02 | Ledger batch 8, cash-out damage ×1.25, entry window 4 s. Requires the ledger host; no value without it. |
| M11 `component_pick_you` 就逮着你揍 (scarce) | B03/B11/B18/C08; S10/S13 | Counter/chain picks the marked recent hostile target for 3 s, damage ×0.85. Scarcity and lower damage trade deterministic target selection for output. |
| M12 `component_owe_it` 这顿打先欠着 (scarce) | Self | Defer 25% of incoming damage for 3 s; healing repays debt before restoring HP. Creates a repayment window, not mitigation. |
| M13 `component_boomerang` 鞋还得捡回来 | S06/S21 | On projectile termination, return toward the caster's foot as it was at termination, same speed/radius, 50% package. First hostile hit can be the original target; elements/burn/grievous carry, slow/control do not. No return if caster is dead/pending death; terrain stops it; the return never returns or pierces. |
| M14 `component_skewer` 一鞋串俩 | S06/S21; S10 | Projectile continues through the first body to 1 additional distinct body at 70%, retaining its slow/control; spread applies only to first contact. On S10, add 1 chain hop at the existing per-hop ratio, included in M07 compression. |
| M15 `component_encore` 再来一遍 | S07/S18/S19 | After 0.8 s, one 50% echo: S18 repeats same-point inner/outer impact/falloff; S07 blasts at release foot with same radius and +1 burn; S19 repeats trap explosion without slow. Host cooldown ×1.15. Echoes do not echo or trigger C06. |

### Connectors (16; a connector changes a result, resource, or later action)

| ID / display | Legal hosts | Exact behavior and counterplay |
|---|---|---|
| C01 `component_fire_skin` 烧着还挺扛揍 | S07/S08; B01/B18/C12 | Fire-source actual damage is converted to a candidate shield: 12 base + 60% of the eligible value, capped at 12% max HP; 1 s interval, 2 s shield. Fire still applies. Interrupt the source or avoid its fire window. |
| C02 `component_ball_kick` 拿你当球踢 | S01/S09; B06/C04 | Real knockback into terrain stuns the victim for 0.6 s; body collision damages both bodies for 30 + 0.2 AD physical. Requires a collision; open space is counterplay. |
| C03 `component_bump_again` 撞着了再撞 | S11–S13 or D | First real dash contact reduces that dash source's remaining cooldown by 1.5 s; 4 s interval. No contact, no refund. |
| C04 `component_roll_bump` 滚过去撞他 | S11–S13 or D | Real dash contact pushes along dash direction 120 cm over 0.12 s. Requires a valid contact path. |
| C05 `component_pin_punch` 摁住再补一拳 | S02–S05/S15; C02/C06 | Two different hard-control sources on one target within 3 s add physical damage: 70 + 0.4 AD + 0.4 AP. Diminishing returns and spacing are counterplay. |
| C06 `component_after_pin` 炸完再摁住 | S08/S18–S20 | After a qualifying zone/trap/impact ends, delay 0.45 s then deal 35 + 0.25 AP magical damage and root for 0.75 s. A missed/avoided area produces no setup. |
| C07 `component_control_mana` 控住薅点蓝 | S02–S05/S15; C02/C06 | Hard control grants 4 MP, max 12 per relevant event budget. Control diminishing returns and mana cap constrain value. |
| C08 `component_breath_strike` 回口气就抽你 | B04/C11 | 50% of actual healing/repayment is stored, up to 60 points; every 1 s, retaliate for the stored amount against a recent attacker within 600 cm. Store expires 4 s after its last addition. Requires healing, an attacker, range, and line of sight. |
| C09 `component_blast_chase` 炸完追着揍 | S07–S10/S13/S20; B01/B10/B11/B18/C12 | Element reaction empowers the next basic for 3 s with 25 + 0.2 AD + 0.2 AP physical damage. Still a basic empower, not a second ability hit. |
| C10 `component_shell_burst` 壳碎了也崩你 | S13/S14; B09/C01 | When its shield is broken, deal 35% of the original shield amount to enemies within 250 cm. Shield expiry/consumption is not a break. |
| C11 `component_hurt_heal` 打疼了我回血 | 18 damaging actives (S01–S10, S12–S13, S16, S18–S22); B03/B05/C02/C05/C06/C08/C10 | Heal 8% of actual HP loss, capped at 30 HP per action. Shield absorption and overkill do not count. |
| C12 `component_dirty_fist` 拳上的脏活也算 (scarce) | The same damaging actives as C11; listed compatible bases B01/B02/B08/B10/B11 | Each eligible skill hit invokes one selected enabled base's effect once, without becoming a basic hit. Requires the base and binding; it does not duplicate its damage identity. |
| C13 `component_next_up` 打完换下一招 | 18 damaging actives above or D | First native direct hit (D: first real contact) reduces every other learned Q/W/E remaining cooldown by 1.5 s, not below now; once per release `EffectId`. Actual contact is required on D. |
| C14 `component_setup_line` 先垫一句 | 18 damaging actives above | Native direct hit marks that target for 4 s. The next different active's native direct hit by the holder gets +25% and consumes the mark. Same host does not consume it; one holder/target/installed instance, refreshed on re-hit. |
| C15 `component_blast_reload` 炸了接着放 | 18 damaging actives above | If the host active is the reaction trigger source, reduce its remaining cooldown by 3 s, once per release `EffectId`. |
| C16 `component_group_refund` 一锅端回本 | 18 damaging actives above | For each additional distinct enemy beyond the first hit by a release, restore 8 MP, max 24 MP per release. Repeat hits/derived hits on one target do not count as additional enemies. |

Card names and one-line descriptions for all items are the `content.display` entries, so runtime card text and the directory are not parallel data sources. All added numeric parameters are in the same authoritative `effects` rows as their operation IDs.

## C. All 22 actives

The numeric changes are active values in `content.json` (fixed terms still scale through the existing `G(L)` curve). “New routes” refer to actual compatible component definitions, not proof of distinct or balanced gameplay.

| Skill | Changed running values / behavior | New route or rationale |
|---|---|---|
| S01 滚远点 | No active value change. | Existing knockback extensions, C02 collision, C13/C14/C15/C16 shared connectors. |
| S02 扒裤子 | No active value change. | Control-duration tradeoff, control-follow-up/mark routes. |
| S03 掏蛋子 | No active value change. | Same control family; hard-control order remains the differentiator. |
| S04 给老子闭嘴 | Cooldown 13 → 11 s. | Stable point-target silence now cycles sooner; C13–C16 can add follow-up resource/tempo. |
| S05 拧你手腕 | No active value change. | Disarm stays distinct from silence/root; generic setup/tempo connectors. |
| S06 甩你一鞋 | Range 750 → 850 cm; projectile radius 45 → 55 cm; AP ratio 0.8 → 0.9. | M13 return; M14 pierce; distance, alternating-active, setup, cooldown/mana connectors. |
| S07 烫你屁股 | AP ratio 0.55 → 0.65. | M15 delayed second ground blast/burn; elemental and active-result connectors. |
| S08 脚底开锅 | Mana 60 → 55; each zone-pulse AP ratio 0.2 → 0.25. | M07 collapse-vs-zone timing; C06 aftershock; C16 multi-target refund. |
| S09 滋你一脸 | Windup 0.3 → 0.2 s; line range 550 → 600 cm. | Knockback/collision route, elemental, setup, and general active-result connectors. |
| S10 电你个哆嗦 | Mana 55 → 50; AP ratio 0.55 → 0.65. | M14 extra chain hop; M07 chain compression; reaction-cooldown/mana/mark routes. |
| S11 贴脸补一拳 | No active value change. | M08/M09 change dash commitment and C03/C04 change contact outcome. It is a dash-to-empower skill, so native-hit-only connectors do not apply. |
| S12 铲你脚脖子 | No active value change. | M08/M09, C03/C04; body-collision semantics remain unchanged this phase. |
| S13 带电撞你 | No active value change. | M08/M09, C03/C04, shield-break, elemental and cooldown routes. |
| S14 锅盖挡脸 | No active value change. | M06 and C10 are its two direct M/C hosts. Defensive utility is distinct, but its direct route count remains low. |
| S15 来打老子啊 | No active value change. | M04 control-vs-cooldown tradeoff and control-follow-up routes. |
| S16 顶着揍 | No active value change. | C11 and generic active-result links; mitigation/recorded retaliation remains the skill's distinctive axis. |
| S17 左右开抽 | No active value change. | Only M06 directly targets this active. B04/B12/C09 can affect the basic package, but this phase did not add a second tactically distinct M/C host route; this remains a clear diversity gap. |
| S18 板砖糊脸 | Mana 65 → 55; AP ratio 0.9 → 1.0. | M15 repeats the delayed impact; area/control and setup connectors. |
| S19 踩一脚试试 | No active value change. | M15 echoes the trap explosion (without slow); trap remains visible and avoidable. |
| S20 满地泼脏水 | Mana 60 → 55; pulse base 18 → 22; pulse AP ratio 0.15 → 0.2. | M07 changes distributed zone to delayed concentrated output; elemental/control/resource links. |
| S21 馊水糊脸 | Range 650 → 750 cm; radius 40 → 55 cm; speed 1300 → 1500 cm/s; base 70 → 80; AP ratio 0.65 → 0.75. | M13 return; M14 pierce; distance, alternation, setup and multi-target connectors. |
| S22 扯嗓子喷你 | Per-wave AP ratio 0.3 → 0.35. | M07 trades three directional waves for a delayed concentrated cone; area/resource links. |

The unchanged skills were not assigned across-the-board damage increases: this pass deliberately concentrated the numeric budget on mage/ranged tools. The unmodified melee/control/dash/defensive skills need movement, collision, and full-build matchup evidence before their base power is changed. That is a sequencing decision, not a finding that they are balanced.

## D. Mage and ranged changes

**Numbers.** Brains AP is 5 → 6 per point (+20%), AP cap 250 → 300. For an all-brains allocation with the unchanged 3 opening points and 3 points per level, the direct AP from points becomes:

| Level | Points | Old AP from points | New AP from points |
|---:|---:|---:|---:|
| 3 | 9 | 45 | 54 |
| 6 | 18 | 90 | 108 |
| 10 | 30 | 150 | 180 |
| 16 | 48 | 240 | 288 |

This is arithmetic from the progression budget, not a combat result. At the normal 16-level budget, the new cap does not bind even if every point is brains; the cap mainly affects additional post-16 investment or other AP sources.

**Active tuning.** S04 cycles faster; S06/S21 have more range and a larger/easier-to-hit projectile, with higher AP payoff; S09 has less windup and more line reach; S10 costs less mana and scales better; S07/S08/S18/S20/S22 gain selected AP/pulse or resource adjustments. This is intentionally not a uniform ranged damage multiplier.

**Construction.** B13 refunds some *actually paid* mana only after a native hit; B14 rewards maintaining distance; B15 rewards alternating Q/W/E; M13 rewards a shot lane that remains clear on return; M14 rewards lined-up targets; M15 trades 15% more cooldown for a delayed second event; C13/C15 convert successful hits/reactions into cooldown; C14 sets up a different ability; C16 rewards distinct multi-target hits. These create resource, timing, position, and target decisions rather than only adding AP.

**Evidence and risk.** Full rules tests prove the implemented gates and arithmetic boundaries, not ordinary-player execution. No new casual/average-player match simulation, installed-build matchup batch, 30-seed paired trial, or human playtest was run in Phase 4. We therefore cannot state that mage/ranged win rates or objective control improved. Higher AP and easier projectiles may amplify late-game area/element combinations; M15's cooldown tax and the limited active budget are not proof that they are safe.

## E. Ten build machines (compatibility sketches, not match-tested builds)

The combinations below use existing Q/W/E skills and target definitions; repeated component IDs are never implied. Compatibility is determined from the current `targets`/`binding` rows. Except for the targeted component-install rules, the listed full three-skill packages have **not** been run through a single end-to-end match or acquisition sequence. “Available” describes material classes, not guaranteed assembly.

| Build | Active set and installed rules | Link / player decision | Counterplay and acquisition |
|---|---|---|---|
| Crit + sustain | S11/S17/S12; B04, B12, M08 on S11, M09 on S12, C08 on B04 | Dash arms an empower; S11/S17 spend empowered basic packages; B04 heals only its own share; C08 turns healing into a delayed counter. | Kite the short range, force the dash to miss, or interrupt stored counter range. Requires three common categories plus a compatible C08 host; no sequence guarantee. **Compatibility only.** |
| Mage cooldown loop | S07/S10/S08; B18 bound to S10/water, M15 on S07, C15 on S10, C13 on S08 | S10 applies/consumes elements; reaction refunds its own cooldown; a later area release cuts other cooldowns; echo trades cooldown for a second threat. | Spread, dodge the telegraph, or avoid leaving a reaction target in range. Requires a binding base, reaction link and echo mod. **Compatibility only.** |
| Element converter | S09/S10/S07; B18 binds S09/fire, M14 on S10, C15 on S09 | S09's added fire can turn its water into a reaction; S10 can chain through other targets; reaction pays back the host cooldown. | Break line, avoid grouped targets, bait the elemental release before entering. Requires S09, a binding base, and connectors/mod; **not balance-tested.** |
| Returning ranged | S06/S21/S04; M13 on S06, M14 on S21, B14, C14 on S04 | Open with stable silence/mark, then choose a projectile lane for return or a lined second body. | Break sight, dodge/step off the return line, hold control cleanse/defense. Requires both projectile mods; **legality from target rows only.** |
| Zone denial | S08/S20/S18; M07 on S20, M15 on S18, C06 on S08, C16 on S20 | Choose persistent lane denial or compressed burst; successful aftershock punishes remaining in the zone; multi-target hit refunds mana. | Leave the marked space before pulses/echo, force the caster away from objective. **No objective scenario run.** |
| Wall-pin | S01/S09/S13; M03 on S01, C02 on S01, C04 on D | Use directional knockback or dash contact to seek a wall/body collision, trading damage for positioning. | Fight away from walls/chokes; don't line up bodies. C02 only works on real collision. **No wall-match run.** |
| Control relay | S02/S03/S04; M04 on S02, C05 on S02, C07 on S03, C14 on S04 | Root, then a different hard control for C05; another active cashes C14's target mark. | Hard-control diminishing returns, spacing, and baiting the follow-up. **Rules logic tested, not a control-chain match.** |
| Dash ingress | S11/S12/S13; M08 on S13, C03 on S11, C04 on S12, C13 on D, B05 | Contact refunds dash cooldown, pushes the target and accelerates other actives once per dash. | Keep distance, block or avoid the trajectory; don't allow repeated body contact. C13's previously missing D contact tracking is now fixed/tested. |
| Shield retaliation | S14/S16/S15; C10 on S14, B16, M06 on S14, C11 on S16 | Defend through a hit window, break a shield near enemies, then heal only from actual ability HP loss. | Withhold damage until shield expiry; split burst; avoid S16's reflected endpoint. **No survival matchup run.** |
| Ledger cash-out | S06/S10/S02; B02, M10 on B02, B03 with M02 on B03, M14 on S10 | Build entries through diverse hits, then choose an 8-entry enhanced cash-out, while M02 delays the separate counter. | Stop hitting for the ledger window, deny the target lane, or move beyond its range. Requires the ledger and counter hosts plus their mods; **no frequency/economy trial.** |

These are ten plausible legal compositions, not ten proven competitive archetypes. The active-target graph has 210 direct M/C host edges in the new data versus 130 before the seven additions (80 added active edges, plus C13's D host). This is a count of compatible attachment edges, **not** a count of genuinely distinct or balanced strategies. S17 still has only one direct M/C host; S14 has two. Both should be early human-playtest priorities.

## F. Acquisition economy

The regular level-up sequence remains “random material category, then deterministic install choice,” not a mixed component three-choice or a shop. Level-up draws now filter each category to common, unowned definitions that have at least one currently legal installation: bases remain legal except B18 without one of its learned active hosts; a mod/connector must target self, D, a learned active, or an owned compatible component, and any extra required base must be owned. Empty categories still drop and weights renormalize; if all common materials are owned the existing point compensation applies. Scarce M11/M12/C12 remain field-reward-only.

| Level band | B / M / C chance | Before | Change |
|---|---|---|---|
| 2–6 | 45 / 30 / 25% | 55 / 25 / 20% | Earlier access to mods/connectors |
| 7–11 | 35 / 35 / 30% | 40 / 30 / 30% | More mod opportunity |
| 12–16 | 25 / 35 / 40% | 25 / 35 / 40% | Unchanged |

Total inventory size grows from 42 to 49; the common pool grows from 39 to 46. The legality filter makes the *level-up draw* dead-host rate zero by construction, but does not guarantee a particular missing component, a complete package, or an alternate target after acquisition. Field rewards and deterministic install behavior were not converted to a different offer flow. No distribution simulation for complete-build probability or rarity dilution was run; the random supply remains a material risk.

## G. Engineering implementation

- **Authoritative content:** `SourceData/Demo/content.json`, `native_ids.json`, `content.schema.json`; seven new component IDs 43–49 and effect-op IDs 149–159; approval decision cap increased to 4096; decision stamp corrected to 2026-11-13. `handler_revision=5`, `projection_revision=6`.
- **Runtime:** compile/projection/import paths; `LDCombatActions`, `LDCombatEffects`, `LDCombatWorkshop`, `LDCombatSkillDirector`, element/keyword directors and cooldown helpers. M13–M15 share release identity and cast ledger; returns/pierces/echoes cannot recursively spawn. C13/C14/C15/C16 use explicit per-release budgets. C14 target-life cleanup follows mark lifecycle; C16 counts distinct target lives, not hit count. C13 D registration now includes `HitHasteOther` when deciding whether to create dash-contact tracking.
- **Tests/scripts:** catalog/parser/schema/Python fixtures updated; every new native rule test added to `Scripts/CheckRuleReport.py`; draw legality and active values pinned. New required native test names: `Lockdown.Rules.Combat.Components.PunchHealOwnShare`, `CastHitRefund`, `DistanceAmp`, `AlternateAmp`, `AbilityHitShield`, `ProjectileReturn`, `PierceAndChainExtra`, `EchoImpact`, `HitHasteOther`, `HitHasteOtherDashContact`, `SetupMark`, `ReactionHaste`, and `MultiHitMana`. The last test exercises actual innate-D release/contact tracking, not only the isolated haste operation.
- **UI:** seven PNG icons were script-derived from existing icons (hue/overlay edits, no generated-image model) and registered with hashes/provenance; seven corresponding UI texture assets imported. Existing component IDs/icons were retained for B reworks.
- **Docs:** `CONTENT_AND_BALANCE.md`, `COMBAT_AND_GROWTH.md`, `GROWTH_TRANSACTIONS.md`, plus this report. Corrected the canonical B17/B18 opening-eligibility text after finding it stale relative to their `SourceData` `opening.actives` lists.
- **Migration/risk:** no persistent player-build migration was added or tested. If any durable blueprint/save serializes B13–B16 by stable ID, the same-ID semantic changes need an explicit migration/version policy. No network authority, Q/W/E/D input model, victory condition, or collision lifecycle was changed.
- **Generated asset note:** `Content/Lockdown/Generated/Data/DA_DemoWorld.uasset` is modified in the worktree and was preserved; `SourceData/World` is unchanged and its generator `--check` passes. Do not discard this user/worktree-owned binary as cleanup.
- **Worktree scope:** this is cumulative Phase 1–4 work, not a Phase-4-only patch. The checkout already contained the evaluation/replay work when this phase continued; nothing was committed, pushed, reset, or stashed. The current changed paths are grouped below.

Brace groups in the table expand each comma-separated basename to the listed directory and suffix; they are path-list shorthand, not literal file names.

| Area | Changed paths |
|---|---|
| Authoritative/generated content | `SourceData/Demo/content.json`, `SourceData/Demo/content.schema.json`, `SourceData/Demo/native_ids.json`, `SourceData/UI/ui_icons.json`, `Content/Lockdown/Data/Demo/demo_content.json`, `Content/Lockdown/Data/Demo/manifest.json`, `Content/Lockdown/Generated/Data/DA_DemoContent.uasset`, `Content/Lockdown/Generated/Data/DA_DemoWorld.uasset` |
| New icon inputs/assets | `SourceData/UI/Icons/icon_component_{blast_reload,boomerang,encore,group_refund,next_up,setup_line,skewer}.png`; `Content/Lockdown/Art/UI/Icons/T_icon_component_{blast_reload,boomerang,encore,group_refund,next_up,setup_line,skewer}.uasset` |
| Combat/runtime C++ | `Source/Lockdown/Private/AI/LDOpponentRequests.cpp`; `Source/Lockdown/Private/Combat/{LDAttributeProjection,LDCombatActions,LDCombatComponents,LDCombatEffects,LDCombatElementsDirector,LDCombatKeywordsDirector,LDCombatMechanicsDirector,LDCombatObservation,LDCombatRest,LDCombatSkillDirector,LDCombatWorkshop}.cpp`; `Source/Lockdown/Private/Development/{LDDemoContentContract.h,LDDemoContentImport.cpp,LDDevGrowthFixture.cpp,LDDevelopmentDirector.cpp,LDGrowthDirector.cpp,LDProgressionRules.cpp,LDSkillGrowth.cpp}`; `Source/Lockdown/Private/Evaluation/LDEvaluationPolicy.cpp`; `Source/Lockdown/Private/Framework/{LDDemoEntry.cpp,LDGameModeRest.cpp}` |
| C++ headers/tests | `Source/Lockdown/Public/AI/LDOpponentPolicy.h`; `Source/Lockdown/Public/Combat/{LDCombatDirector.h,LDCombatElements.h}`; `Source/Lockdown/Public/Development/{LDDemoContentDefinitions.h,LDDemoContentIds.h,LDDevGrowthFixture.h,LDDevelopmentDirector.h,LDGrowthDirector.h,LDProgressionRules.h}`; `Source/Lockdown/Public/Evaluation/LDEvaluationPolicy.h`; `Source/Lockdown/Private/Tests/{LDAttributeProjectionTests.cpp,LDBuildEvaluationProbe.cpp,LDBuildEvaluationProbe.h,LDBuildEvaluationTests.cpp,LDCombatEvaluationTests.cpp,LDBuildPrimitivesTests.cpp,LDDemoContentCatalogTests.cpp,LDGrowthRulesTests.cpp}` |
| Evaluation/test scripts and manifests | `Scripts/BuildEvaluationLockdown.py`, `Scripts/CheckRuleReport.py`, `Scripts/DemoContent.py`, `Scripts/DemoContentTestFixtures.py`, `Scripts/ExportDemoNativeFixtures.py`, `Scripts/TestBuildEvaluationLockdown.py`, `Scripts/TestDemoApprovedImport.py`, `Scripts/TestDemoNativeFixtureImport.py`, `Scripts/CombineBuildEvaluation.py`, `Scripts/TestCombineBuildEvaluation.py`, `Scripts/BuildEvaluationManifests/phase2-b04-c08-smoke.json`, `Scripts/BuildEvaluationManifests/phase2-fixed-opening.json` |
| Documentation | `docs/demo/COMBAT_BALANCE_AUDIT.md`, `docs/demo/COMBAT_BALANCE_PHASE2.md`, `docs/demo/COMBAT_BALANCE_PHASE3.md`, `docs/demo/CONTENT_AND_BALANCE.md`, `docs/demo/COMBAT_WORKSHOP_OVERHAUL.md`, `docs/systems/BUILD_EVALUATION.md`, `docs/systems/COMBAT_AND_GROWTH.md`, `docs/systems/GROWTH_TRANSACTIONS.md` |

## H. Validation and reproduction

**Final checked inputs:** Demo content root SHA-256 `f35d3a748c11c74199ca3fc62063618a57173955feeaac64a79e21d1e2a153cc`; generated payload SHA-256 `ad7e9bdb24603a33e41e95240014977fbc42cf290bacfa80fea13cf4def449c9`; display SHA-256 `cb28bf56b889aa6fa8b28e58d7e3ef15adf4a58797b50d81aeda29b6a84ace1f`; native contract SHA-256 `5c0fef1c64f72e68f3253fc4a5341aefe19db89abac15961031333505cf1f711`; native IDs SHA-256 `0a9fff94ff0c2a442150703dd747b81d9ceb96bb9a5aa704358a47ac7c4e7c42`; world payload SHA-256 `db78ac54f49fb3f310889315bbead6458b473b14803f94bac62cabe308edfe1a`. Native fixture cache profile: `1d99775cc36350ecf0f22b8b05cfad0828aed27b3566ca498af53d015260d402`. Checkout HEAD at task start was `b8a55b51` on `sinmentis/combat-balance-audit`; changes remain uncommitted. Engine: Unreal 5.8.4, `++UE5+Release-5.8-CL-58902052`; `UnrealEditor-Cmd.exe` SHA-256 `02A0D0E50381CA631BE0221486247AE8BFE37CD4AD59F52B64CBF391D612B8BB`.

| Evidence | Result | Boundary |
|---|---|---|
| `python -B Scripts\SyncDemoContentData.py --check` | Pass; final root hash above | Verifies generated content consistency, not gameplay. |
| `python -B Scripts\SyncDemoWorldData.py --check` | Pass; world hash above | Source world data did not change. |
| `.\Scripts\BuildEditor.ps1 -FullUnity -MaxParallelActions 4` | Pass; `Artifacts\20261113-114227-400-BuildEditor` | Full-unity compilation, not a packaged-game check. |
| `.\Scripts\BootstrapAssets.ps1 -Mode RefreshDefinitions` | Pass after the final editor build | Rebuilds content definitions. |
| `.\Scripts\BootstrapAssets.ps1` | Pass; `Artifacts\20261113-113302-497-Bootstrap-Validate` | Asset/content validation; not visual playtesting. |
| `.\Scripts\RunTests.ps1` | **396/396 required native rules passed** (381 success, 15 success-with-warnings, 0 failed), `Artifacts\20261113-114428-624-Automation` | Includes 15 automation warnings; no rule test failed. This is not overall balance proof. |
| `python -B Scripts\RunPythonTests.py` | **316/316 passed**, `Artifacts\PythonTests\20261113-114817-24548.log` | Exit code 0. The suite emitted combination-refusal/threshold diagnostics and a non-fatal `WinError 5` warning while updating `last-failed.json`; that bookkeeping warning did not change the test result. |
| Paired combat scenarios / casual-AI / human playtests | **NOT TESTED in Phase 4** | No conclusions on win rates, hit rates, objective value, multiplayer ecology, or fun. |

`git diff --check` by itself reports CR characters as trailing whitespace on the byte-sensitive CRLF `content.json` edits. The non-mutating CRLF-aware equivalent, `git -c core.whitespace=cr-at-eol diff --check`, passes; source line endings were preserved. Both content/world sync `--check` commands pass.

Reproduce the native gate with a fresh PowerShell process:

```powershell
$env:LD_UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
python -B Scripts\SyncDemoContentData.py --check
python -B Scripts\SyncDemoWorldData.py --check
.\Scripts\BuildEditor.ps1 -FullUnity -MaxParallelActions 4
.\Scripts\BootstrapAssets.ps1 -Mode RefreshDefinitions
.\Scripts\RunTests.ps1
python -B Scripts\RunPythonTests.py
```

The Phase 3 harness remains a separate evaluation facility. No Phase 4 run with installed components or paired combat seeds was produced, so it must not be cited as evidence for this overhaul.

### Remaining problems and next test priorities

1. Run packaged or native controlled matches with installed builds at L3/L6/L10/L16, Casual/Average/Skilled policies, paired seeds and spawn swaps. Include projectile aim/dodge, dash pursuit, wall/body collision, 3–6 player FFA, choke/objective entry/hold/retake, and mana/cooldown sustainability.
2. S17 has only M06 as a direct M/C host, and S14 only M06/C10; these are weak points against the desired per-active route diversity target. Do not disguise B04/B12/C09 basic-package effects as multiple active modifications.
3. Check M05 near its 1000 cm cap: it can impose the 20% damage loss without adding actual range. A zero-benefit install guard is still absent.
4. Revisit B07 area compatibility, C01 fixed-window expiry, S10 derived-hit responses (B02/B18/C12), S12/S13 real body-collision semantics, and S14 interaction with S21's attack-speed reduction. Those prior audit questions were not re-tested by this phase's rules gate.
5. Test B04 with S11/S17, crit, and components in the same actual combat sequence; verify the retained crit identity feels rewarding without making basic-attacks universally dominant. No real matches establish this yet.
6. Icon validation passed through Bootstrap, but import-time art-index behavior should be rechecked after any map/art catalog changes. No map changes were made here.

## Five direct answers

1. **How many more genuinely different possibilities?** Seven components add 80 direct active-host attachment edges (plus a D host); active-target edges rise from 130 to 210. That is the defensible structural count, not 80 proven playstyles. Meaningful strategic diversity remains unmeasured, and S17 still has only one direct M/C route.
2. **Mage/ranged improvements?** Brains is 6 AP/point (cap 300), and S04/S06/S07/S08/S09/S10/S18/S20/S21/S22 received the numeric changes in Reports C–D. M13 return, M14 pierce/extra chain, M15 echo, B13–B15 and C13–C16 add projectile, timing, mana and cooldown links. No ordinary-player match effect has yet been quantified.
3. **Is basic attack still unconditionally superior?** Not established either way. B04 now heals only its own package share and shields do not count as HP loss, but its 12% basic lifesteal and empower crits remain. Full rules tests prove mechanics, not matchup ecology.
4. **What new machines can players create?** Returning/piercing S06/S21, echoing S07/S18/S19, reaction-cooldown S10, alternating-active setup, distinct-target mana refunds, and D-contact cooldown/knockback combinations are now representable. Report E gives ten legal compatibility sketches; none is claimed match-validated.
5. **What remains dull or unbalanced?** S17's only direct mod route is M06; S14 has two. M05 can be strictly negative at its range cap. Several basic-centric components, B07 area compatibility, C01 window edge, S10 derived-event interactions, collision builds, and S21/S14 status interaction remain untested. No overall balance claim is made.
