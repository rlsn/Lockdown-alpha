# Combat runtime, statuses and counterplay

Retain the complete v0.7 action/damage/protection/targeting contracts unless overridden here. The original archive's02,03,04,05,06,11,16 documents supply unchanged operation semantics; v0.8 data is the only active tuning catalog. No engine-framework rewrite is implied. Direct casts use the existing native movement and targeting architecture.

Validate match/owner/life/sequence, equipped source, target permissions, natural gate, CC, resource and maximum cooldown endpoints. Reserve field budget before committing a cast that needs cells. Accept visible windup; revalidate dynamic range/sight/source at release. At release spend MP/charge and set cooldown atomically; channels spend at start. A pre-release cancellation spends nothing and enters400 ms recovery. Released miss/block never refunds. Same request sequence produces one receipt. No client clock backdating. World VFX only serialize to authorized viewers.

## Effects and action categories

| Status | Core action rule | Removal | Heart claim |
|---|---|---|---|
| Stun | No walk/basic attack/learned action/item action | Strong negative dispel externally; preemptive guard | Interrupts |
| Hex | No attack/learned/item/mobility; walk at50% | Strong | Interrupts |
| Silence | Blocks all learned skills, martial/profession included | Basic | Alone does not interrupt |
| Mute | Blocks all item actions, including body actions on skill bar | Basic | Alone does not interrupt |
| Root | Blocks voluntary walk/dash/blink/gate | Basic | Stationary claim continues |
| Slow | Strongest eligible slow, max60% | Basic if unit debuff; field-bound contact reapplies | Continues |
| Disarm | Blocks basic attacks | Basic | Continues |
| Break | Suppresses tagged item behavior/procs/auras only | Strong | Continues |
| Blind | Own sight200 cm; does not alter other authorized sensors | Basic | Continues |
| Reveal | Removes concealment within source geometry | Source-bound | No identity/build leak |
| Grounded | Blocks voluntary dash/blink/gate; walk allowed | Leave/destroy source | Continues |
| Wet | Setup tag; no default movement/resistance penalty | Basic; river/rain may reapply | Continues |
| Burning |12 raw magical damage/s for3s, strongest source | Basic or Water | Damage alone continues |
| Displacement | Swept validated push/pull/swap | Cannot retroactively dispel | Any actual position change interrupts |

All ordinary learned skills are silenced regardless of martial/spell flavor. This explicitly resolves the earlier proposal for silence-immune martial skills: no broad exception in v0.8. Item actions are Mute-blocked and not Silence-blocked, regardless of which bar holds them. Stun/Hex prevent both. Break never removes armor, core weapon damage, attributes, granted action access or the intrinsic trait. Fear/new Evasion RNG/full elemental resistance and subclass mechanics remain deferred.

Hostile unit-target spell packets: reflection/dome where applicable → spell block → snapshot Wet and elemental reaction eligibility → guard/target-category immunity → damage/status components in authored order → commit unit reaction → request finite surface transition. Reflection at most once; reflected source is reflector for damage/field attribution, original caster now valid hostile victim. Area/ground hits bypass targeted spell block. Blocked packet creates no impact field from that packet. Arcane Prison/ground fields are not projectiles. Rooted unit can be pushed; Anchor blocks push/pull only, not stun or explicit Interrupt. Boss/towers immune to hard-control/status disables per inherited categories, but take legal damage; protected officials do not become attackable through surfaces.

One guard charge strips next Stun/Hex component; damage and Wet consumption still occur if reaction attempt otherwise eligible. General barriers remain strongest pool only,40%maxHP cap, no CC immunity. Ordinary physical and magical mitigation each capped60%; no new immunity item. Direct physical skill amounts do not automatically receive INT multiplier; only magical/heal/barrier skill amounts do. INT multiplier cap1.25, strongest gear spell amplification up to10% after it. Item damage/heal/barrier uses flat item numbers, no INT or mastery. Radius/duration/cooldown never get generic INT scaling.

Power Strike/Oath Mace barrier bonus: for base post-mitigation damage D and eligible bonus b, absorb A=min(pool, D*(1+b)); base damage spent on barrier=min(D, A/(1+b)); HP receives D−spentBase. No bonus spills into HP. Round fixed point at final receipt. Directional shield/armor combine before the same60% mitigation cap. Zones/DoTs apply per original-life/tick once, never once per collision callback.

## Control-chain policy

Normal hard-control DR remains off. Never sum same-status durations; retain independent expiries and strongest magnitude. Wet reaction has its own6000 ms victim lock across all sources, not a chainable damage tick. Stun600 ms is fixed across ranks, quality, traits, items and attribute growth. Other hard-control durations retain their listed trained-rank bases. Capture tests/telemetry must measure cumulative disable time from different skills; a specific Wet lock is not proof broader chains are balanced.

Basic cleanse removes eligible Slow/Root/Silence/Mute/Disarm/Blind/Wet/Burning/marks; Strong adds Stun/Hex/Break. Positive dispel removes ordinary barriers/Anchor; it does not clear map cells or destroy emitters. A normal self-cleanse cannot activate while stunned even if Strong could remove the status. Friendly removal/pull/pulse buff needs the explicit aid-consent capability (self always allowed), default off and revoked on either life change. In FFA there are no assumed teammates.

Generic Heart claim remains10s; no attacks/casts/items while claiming. Damage, Wet, slow, root, silence and mute alone do not cancel. Explicit Interrupt,600 ms Wet-stun, Hex, death, nonzero forced movement, intentional self-cancel or access loss reset progress. Precast fields/buffs may persist; no resuming cancelled progress. Follow inherited inclusive deadline/tie ordering.
