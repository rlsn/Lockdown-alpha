# Bounded environmental rules and reaction table

Five interaction families Fire/Frost/Lightning/Wind/Nature, plus Water as setup/extinguish medium. Damage remains physical/magical/pure; elemental tags never add independent resistance layers. v0.8 introduces no new pure-damage source. Six cell conditions, one finite Oil overlay, ten reaction rules. See04 for unit-contact/state ordering.

| Reaction ID | Inputs | Outputs | Unit behavior | Bound |
|---|---|---|---|---|
| water_frost | water_or_wet, frost_input | frozen | Direct hostile Frost on Wet attempts600 ms stun; surface contact slow30% only | Keep underlying water; no bridge/navmesh |
| ice_fire | frozen, fire_input | wet, obscured | No extra impact damage from melting | Remove Frozen; Steam3000 ms; no Burning that batch |
| water_fire | water_or_wet, fire_input | obscured | Fire damage still resolves; clears unit Wet after hit | Steam3000 ms; no Burning on water |
| water_lightning | water_or_wet, lightning_input | electrified | 18 raw magical damage/s contact, no hard control | No spread beyond footprint or between rivers |
| oil_fire | oil, fire_input | burning | 20 raw magical damage/s contact | Consume Oil; Burning6000 ms |
| plant_fire | overgrown_or_flammable_socket, fire_input | burning | No separate initial bonus damage | Clear temporary growth; Burning3000 ms; static trees unchanged |
| water_extinguish | burning, water_input | wet | Clears unit Burning on Water hit/contact | Oil consumed by burning is never restored |
| wind_obscured | obscured, wind_clear | clear | No reveal debuff; vision recalculates | Clears only selected cells; no gas relocation |
| ice_impact | frozen, shatter_input | wet | Shatter Burst80 raw physical once/unit/cast | No navigation collapse; no stun |
| growth_soil | soil, nature_input | overgrown | 15% surface slow, direct Entangle root separate | No new terrain cover/LoS/collision in v08 |
