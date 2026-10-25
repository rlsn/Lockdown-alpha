# Acquisition, assembly and economic integration

Preserve v0.7/v0.6 actual fixed supplies, regional quote logic, finite material sources, work/procurement, bank, ownership and no dialogue. New gear is outside automatic commodity buyback. Existing books/trainer prices and rare capstone sources remain; new 52 catalog books use the same circle policy. No duplicate-book rank grinding. No new reward mint or unlimited sale sink is introduced by the expanded catalog.

| Output | Inputs | Gold burned | Station / hold |
|---|---|---|---|
| harpoon_halberd | {"iron_halberd":1,"grappling_chain":1} | 8 | town_workshop / 4000 ms |
| conduit_staff | {"simple_staff":1,"Crystal":1,"Toolkit":1} | 8 | town_workshop / 4000 ms |
| null_staff | {"simple_staff":1,"null_bell":1,"Toolkit":1} | 8 | town_workshop / 4000 ms |
| rift_staff | {"simple_staff":1,"grappling_chain":1,"arcane_catalyst":1} | 10 | advanced_forge / 6000 ms |
| oath_mace | {"iron_halberd":1,"Toolkit":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| null_dagger | {"watchblade":1,"counterglyph":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| reflective_mail | {"bastion_plate":1,"mirror_sigil":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| anchor_greaves | {"pathfinder_boots":1,"Ore":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| scholars_sigil | {"runic_staff":1,"Core":1,"arcane_catalyst":1} | 10 | advanced_forge / 6000 ms |
| iron_oath | {"bastion_plate":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| blink_charm | {"grappling_chain":1,"Core":1,"arcane_catalyst":1} | 10 | advanced_forge / 6000 ms |
| heartbreaker | {"Core":2,"arcane_catalyst":1,"heart_fragment":1} | 20 | heart_forge / 8000 ms |
| frost_staff | {"simple_staff":1,"Crystal":1} | 6 | town_workshop / 4000 ms |
| conductive_spear | {"iron_longsword":1,"Ore":1,"Crystal":1} | 6 | town_workshop / 4000 ms |
| thermal_coat | {"reinforced_leggings":1,"Fiber":1,"Toolkit":1} | 6 | town_workshop / 4000 ms |
| elemental_purifier | {"water_flask":1,"Toolkit":1,"Crystal":1} | 8 | town_workshop / 4000 ms |
| frost_prism | {"frost_capsule":1,"Crystal":1,"Toolkit":1} | 8 | town_workshop / 4000 ms |
| mirror_amulet | {"mirror_sigil":1,"Core":1} | 10 | advanced_forge / 6000 ms |
| command_banner | {"Fiber":2,"brokers_seal":1} | 6 | town_workshop / 4000 ms |
| resonance_ring | {"Crystal":1,"Core":1} | 10 | advanced_forge / 6000 ms |

Every new item has an explicit source in items.json. Priced assembled items may be bought complete at OfficialSupply or assembled as alternatives; one transaction selects one route. Buy-completed work orders for any non-T4 recipe purchase only missing immediate official inputs and burn their prices plus fee at that recipe station. Buyer supplies Core/Toolkit/Crystal/catalysts and other nonofficial inputs. No recursive supply fabrication, no Heartbreaker shortcut. All inputs unbound/unreserved; stage lineage/output and validate final count/weight before one atomic commit. No disassembly. Assembly outputs Standard regardless of consumed quality.

Standard/Fine/Masterwork quality remains1/1.07/1.14 on explicitly listed quality_fields only: base weapon damage, flat armor, and eligible item barrier amount. Attributes, procs, elemental durations, control, cooldown, movement, range and skill rank never scale with craftsmanship. Ineligible quality upgrades reject without spending. Same item ID and readiness endpoints persist. Fees/holds/materials remain in acquisition.json; Broker trait and Broker Seal provide one strongest−1 gold discount, minimum1.

Every Circle-I book40 gold at City; II60 at West/East; III90 at East Archive with2 copies/definition globally; IV one existing assigned POI copy. Read3000 ms after6s quiet; duplicates retain book. All taught skills are Rank1. Twelve+ recipe chains remain bounded by actual stock and travel; no free new starting kits. Gear and counters compete with Charter money.

Economy accounting remains goldEnd=goldStart+rewardMints−burns. New maximal combat reward gold stays642, total reward upper bound540*N+682; expanded items add sinks, no sources. No XP for combining, quality, markets, repeated Wet/Frost, field damage to self, or trait use. NPC residents receive no automatic new wallet/gear grants; contenders obey same caps/costs. Physical field emitters are transient entities linked to spent source receipts, not lootable duplicated item instances.
