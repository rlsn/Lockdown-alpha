# Active continuation — v0.8 B84

Base: main at c88b2b4 (B83). Current implementation and exact validation scope
are in [B84_IMPLEMENTATION](v08/B84_IMPLEMENTATION.md). Original specifications,
data/templates/provenance, previous payloads/evidence and v07 assets stay frozen.
The [working ledger](v08/FeatureLedger.csv) records focused engineering scope;
full release acceptance and named inherited deferrals remain open.

## Current native baseline

Explicit `?Profile=v08_integration` uses L_V08Region, schema8 and checksum-closed
combat_b84/equipment_b84/acquisition_b84 UFS payloads. SyncB84Data.py is the active
checker. All 52 catalog abilities, 80 items, 20 recipes and nine presets are
available. Schema8 has 62 rank identities, including nine legacy economic/basic
adapters and None; schema7 remains 38. All adopted numeric IDs remain stable.
Current clients need matching modules; no old-executable/live-match migration or
new reconnect mechanism is claimed. Default frontend/V05 and explicit v07
remain separate. Historical checkers/payloads describe their own snapshots.

B84 completes the elemental utilities, support/construct skills, all 18 equipment
modifier paths and basic-hit procs. Ember Line's committed finite damage zone
survives source death with original-life attribution. Ordinary constructs remain
life-bound. Conduit grants bounded consenting recovery; Sabotage suppresses
construct effects without deleting/stealing them. Rescue uses native sweeps;
Rally is one barrier pulse. New construct targeting uses mode10 in schema8 only.

Modifier derivation uses equipped physical instances; Break suspends declared
modifiers, not ordinary stats or immutable class traits. New reductions compose
inside the ordinary 60% cap, and Storm/Insulation are strongest rather than
multiplicative. Proc owner-family endpoints persist in bounded owner entries;
physical proc endpoints travel with the same instance. Quality retains both
endpoint types. Aura membership uses current consent and geometry. Goggles use
already-explored static substrate only, never hidden dynamic field occupancy.

The v08 acquisition catalog has all 20 recipes. `RecipeCode/RecipeIndex` preserve
old codes20–31 and quality/cache codes32–35; new recipes use36–43. Work orders
buy only missing immediate official inputs at the correct non-T4 station; v07
keeps its East-only rule. Exact unbound/unreserved inputs, finite books/materials,
Standard outputs, parent lineage, quality eligibility, no-XP assembly and gold
conservation remain mandatory. There is no recursive material fabrication or
Heartbreaker shortcut. No map or v07 assets were regenerated for B84.

B81 six typed actions / eleven bindings, B82 movement/Wet/direct-Frost ordering,
and B83 six-state/Oil grid, paid projectile reservations, 64/256/512 caps,
original-life damage and symmetric smoke union authorization remain in force.
Do not introduce dynamic terrain/nav changes, hidden source replication or extra
gear buttons while integrating world content.

## B85 boundary

1. Demonstrate the existing four legal Heart routes through normal v08 sources,
   costs, travel and inventory, without grants or diagnostic progression setters.
2. Integrate the full catalog into world encounters, finite sources, NPC behavior,
   death/return and Heart interruption. NPCs receive no automatic wallet/gear
   grants. Preserve inherited control-category immunities rather than inventing
   broad new immunity rules.
3. Record one low-level tactical Heart interruption and reconcile complete
   source/sink/physical-identity ledgers for each route. Reuse retained world
   authority instead of parallel simulated browser mechanics.
4. Carry forward per-definition independent-client/adverse cases, proc transfer/
   death timing, prior-life/protection/hidden-chain combinations and the historical
   intermittent v07 held-preparation concern. B84's current v07 runs pass; that
   does not establish a root cause for the older failure.
5. B86 owns rendered input/usability, human feedback, cold packaged distribution,
   long sessions and full four-contender service/replication/frame-rate evidence.

## Commands and working rules

```powershell
python -B Scripts/SyncB84Data.py --check
python -B Scripts/TestV08Import.py
python -B docs/v08/tools/check_spec.py
./Scripts/BuildEditor.ps1 -MaxParallelActions 2
./Scripts/BootstrapAssets.ps1 -Mode Validate
./Scripts/RunTests.ps1
./Scripts/RunB84Check.ps1 -RoundTripMs 100 -JitterMs 10 -TimeoutSeconds 180
./Scripts/RunB83Check.ps1
./Scripts/RunB81Check.ps1
./Scripts/RunLoadoutCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunAcquisitionNetworkCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/OpenStandalone.ps1 -Profile v08_integration
```

Run Unreal operations sequentially and keep Source/Config frozen during each
native build/check. UE5.8.4 CL58902052, Win64 Development,
MSVC14.44.35229 / SDK10.0.26100.0. At most4 compiler actions; B84 used2 under
memory pressure. If world authoring changes, rebuild only the explicit v08 map
with `BootstrapAssets.ps1 -Mode Generate -RebuildV08Region`, then validate all
maps. Preserve historical assets, payloads, evidence and original templates.

B84's fixture includes real paid transactions, held recipes, actual native
effects and remote movement. Catalog-lane wallet/input/knowledge/equip/HP/status/
velocity/position/clock setup is diagnostic and never normal earned progression.
See the report and evidence summary for commands, counts and their exact limits.
E01–E48, V07-D01–D08 and V06-D01–D08 remain mapped and preserved; focused native
checks do not waive full release acceptance or named deferrals.
