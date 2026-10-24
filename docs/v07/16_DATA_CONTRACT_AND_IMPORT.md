# Data contract and engine import

All catalogs use schema_version7, stable snake_case IDs and cm/ms units. JSON is source tuning, not a shipped game framework. Validate before import; reject unknown operations rather than showing a fake working ability. Every operation needs one real server implementation and native feedback.

| Operation | Meaning |
| --- | --- |
| damage | One immediate typed damage component, mitigation then barrier then HP. |
| status | Apply named status with source/life, duration and dispel family. |
| directional_barrier | One capped directional pool for fixed lifetime/front arc. |
| projectile_damage | Launch snapshot-trajectory projectile; typed damage on legal original-life hit. |
| interrupt | Cancel current eligible windup/channel even without a hard status. |
| spawn_ward | Create destructible geometry-limited sight source, owner limit and expiry. |
| spawn_trap | Create attackable detector-visible trigger; one eligible target root then consume. |
| trail_mark | Basic-dispellable target mark; publish only geometry-authorized stale trail cues. |
| barrier | Apply strongest capped general damage pool with expiry; no control immunity. |
| decoy | Observed plausible pathing proxy with one HP, no rewards/interactions or vision. |
| smoke | Create timed geometry occluder; no unbounded reveal blocker beyond authored volume. |
| basic_dispel | Apply listed target-polarity and aid-consent checks; remove basic instances only. |
| detector | Timed geometry-limited sight/reveal source; never identity/wall sight. |
| damage_zone | Bounded per-target total split into duration/tick count; rank damage overrides total_damage. |
| grapple | Target mode selected on arm; hostile pull is blockable packet, terrain self movement obeys root/grounding. |
| grounding_anchor | Attackable emitter applies source-bound mobility prohibition within geometric radius. |
| push | Cone selects eligible targets then collision-sweeps each; actual movement interrupts. |
| swap_self_owned_marker | Two-phase placement/execution with separate listed costs and full CD on execute/enemy destruction. |
| dash | Native collision-swept voluntary movement, no teleport through walls. |
| remedy | Consented-other strong negative dispel, self short next-stun/hex guard, no stunned self activation. |
| prison | Attackable anchor creates walking collision ring; never overlap body, no sight wall. |
| telegraph | Visible windup only within normal authorized world audience; no separate resource commit. |
| area_status | Apply named status to legal AoE targets; targeted spell block bypassed. |
| projectile_dome | Attackable emitter blocks hostile projectile boundary crossings, not walkers/instant/cones. |
| swap_two | Two target endpoints validated atomically; either blocking charge cancels all, no half swap. |
| linked_gates | Atomic endpoint pair, explored/static targeting and constrained voluntary held travel. |
| arm_next_hit | Timed owned weapon buff; next attack hit applies blockable silence packet, full CD at arm. |
| reflect_next_projectile | Short one-charge projectile reflection, at most one reflection per packet. |
| anchor | Timed push/pull resistance, self slow, no stun/hex/swap/explicit interrupt immunity. |
| basic_dispel_self | Item basic negative self cleanse; mute/stun/hex prevent activation. |
| heal_over_time | Fixed total divided over lifetime; positive damage ends remaining ticks. |
| restore_mp | Interruptible held item action restoring fixed MP up to max at completion. |
| recall | Held movement to visited waypoint, full endpoint validation, no objective cargo, damage interrupts. |
| targeted_mute | Observed unit-target status, basic-dispellable and spell-blockable. |
| spell_block | One targeted-packet charge, no stacking; consumed on first eligible hostile packet. |
| control_guard | One next-stun/hex component charge, accompanying other effects still apply. |
| terrain_grapple | Terrain-only native swept self movement, no target pull. |
| blink | Instant validated endpoint within range, root/grounding and direct-damage lock apply. |

## Required importer behavior

Ability requirements_any is OR over AND clauses; `[{}]` means no natural gate. Range0 is self-only, area0 is no radius. Cone angles are declared by effect angle_deg, never by area_cm; area_cm always remains a distance. Ground/unit/line/two-target/marker modes must be separate validated target variants. One rank_scaling entry gives the Rank 1/2/3 base override: damage maps to amount or total_damage, barrier to pool amount, duration to primary authored lifetime, cooldown to action endpoint, radius to primary radius/vision radius, anchor_hp to destructible emitter HP, stun_ms to stun duration. For Farwatch duration applies lifetime, not arbitrary tags; for Ledger Mark its mark duration, not clue lifetime. A potency bonus only selects a higher base for fields in potency_fields, never the trained-only fields. Rank values are not cumulative multipliers.

For an ability with no rank scaling, Rank 2/3 are not offered in UI and no mastery points may be spent; initial catalog currently supplies a scaling field on every new ability. Unused numeric fields must not be invented from display text. Brace Wall/Surety Shell numeric pools use skill INT scaling, item pools do not. Grounding Field radius uses area_cm and operation radius_cm consistently. Where a rank-overridden radius exists only in area_cm, importer copies it to the emitter volume and target preview; it must not leave a stale smaller detection radius. Temporal Lock uses area_cm even though area_status has no redundant radius field.

Item definition binds `charm` into either charm_a/charm_b, never both. JSON null active means no item-use action. `official_price_gold: null` requires recipe or named objective source; it is not free. Book wrapper is generated per ability ID with weight1, taught ability, Rank 1, source/price/stock and drop=true; legacy book IDs remain actual audited definitions. Material definitions keep case-sensitive v0.6 IDs. Never normalize Ore/Core/Fiber into conflicting duplicate definitions. No new gear/book commodity-market eligibility.

Input references must resolve exactly once. Definition IDs are public; instance ownership/learned lists stay private. Instance record has itemId, definition, quality, owner/location, equippedBinding if any, cooldown endpoint, lineage parents, revision and reservation. No duplicate item across grid/equipment/quick references. Recipes stage output and all lineage before consuming. Maximum signed integer/overflow validation remains on every gold/count operation. Game time is integer ms, source JSON seconds fields are explicitly converted once.

The companion model checker validates document/data consistency and small reference examples; its helper functions are not a replacement for server implementations. Config import must be verified against actual assets and checksums in the final report. Run all required engine/network/human checks separately.
