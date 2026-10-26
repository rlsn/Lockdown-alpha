# Active continuation — v0.8 B83

Base: main at a1cb64f (B82). B83 implementation and exact validation scope are
recorded in [B83_IMPLEMENTATION](v08/B83_IMPLEMENTATION.md). The normative
[B80–B86 plan](v08/11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md), original inputs,
templates, archives, provenance and historical payloads/evidence remain frozen.
The working [FeatureLedger](v08/FeatureLedger.csv) records focused engineering
results without rewriting specification templates or full release acceptance.

## Current native slice

Explicit `?Profile=v08_integration` uses L_V08Region and schema8
combat_b83/equipment_b83 UFS payloads. SyncB83Data.py checks the active importer
and sparse native reservations. B81 payload checking remains historical;
SyncB82Data.py intentionally describes its old native enum snapshot and is not
the current checker. TestV08Import isolates B80 retained prefixes in temporary
fixtures. Explicit v07 uses unchanged L_V07Region and schema7 payloads; default
frontend/V05 remain separate. Do not migrate live v07 matches.

The slice exposes 35 learned abilities, 61 items and nine presets. Schema8
ranks have 54 sparse identities; schema7 stays at 38. Firebolt39, Gust41,
Entangle42, Chain Lightning43, Shatter Burst52, Conductive Field53, Oil Flask91
and Gust Vial93 now have native handlers and paid acquisition. Ember Line and
other unsupported definitions stay hidden pending complete B84 migration.
All clients need matching native modules; old executable interoperability and
a new reconnect mechanism are not claimed.

B83 extends the 200cm grid at (-6000,-4000) to Wet/Frozen/Burning/Electrified/
Obscured/Overgrown and finite Oil, with all ten nonrecursive reactions. Limits
are 16 cells/new action (32 Smoke), 64 distinct sponsor-cells, 256 match cells
including reservations, 512 evaluations/100ms, and 500ms contact damage.
Paid delayed projectiles hold capacity until impact/block/miss; reflection
changes damage creator and retains the original budget sponsor. A private
presentation pool is allocated before costs. Source death does not destroy
committed fields or change their original-life attribution. Static river,
soil, authored fuel and metal do not acquire dynamic terrain/nav semantics.

Environmental damage uses ordinary mitigation/reward ledgers, affects its
creator and respects protection. Surface Burning20DPS replaces unit Burning12;
Electrified18 is separate. Smoke/Steam blocks external cell-union boundaries
symmetrically; internal shared boundaries are transparent. Gust recomputes sight.
Only authorized public cue tuples are sent, with a bounded 512-entity wire;
canonical grid, source life and sponsor stay server private. Owner committed
masks contain only geometric cells, not hidden occupants or attribution.

B82's actual movement ordering, Wet refresh/residual, Frozen slow and direct
Frost's 600ms attempt / contender-wide 6s endpoint remain. Environment contact
never Frost-stuns. Earlier combat drains against the old pose before authority
movement; actual contact refreshes after movement. Contact/expiry precede direct
packets, then field commands/damage, deaths and Heart resolution.

B81's six typed actions, eleven bindings, immutable trait, quiet atomic drafts,
source invalidation, item identities, consumable clearing/death rules and
absolute cooldown endpoints remain in place.

## B84 boundary

1. Complete the authored 52 abilities / 80 items / 20 recipes, enabling only
   definitions with real native effects, acquisition, sources and UI support.
2. Finish remaining modifiers/procs/support semantics and migrate Ember Line's
   entire damage-zone lifecycle. Preserve stable numeric IDs and schema7 paths.
3. Verify every definition's obtainable source, rank gates, quality/recipe costs,
   item conservation and meaningful visible effect. Do not count diagnostic
   grants as normal earned journeys.
4. Carry forward adverse timing/privacy matrices and full four-contender
   performance/replication/frame-rate acceptance. B83's focused maintenance
   timing is not a full performance claim.
5. B85 owns complete four-route no-grant world journeys and source/sink audits;
   B86 owns packaged/native usability, human feedback and release evidence.

## Commands and evidence limits

```powershell
python -B Scripts/SyncB83Data.py --check
python -B Scripts/TestV08Import.py
python -B docs/v08/tools/check_spec.py
./Scripts/BuildEditor.ps1 -MaxParallelActions 2
./Scripts/BootstrapAssets.ps1 -Mode Validate
./Scripts/RunTests.ps1
./Scripts/RunB83Check.ps1 -RoundTripMs 100 -JitterMs 10 -TimeoutSeconds 180
./Scripts/RunB82Check.ps1 -RoundTripMs 100 -JitterMs 10 -TimeoutSeconds 180
./Scripts/RunB81Check.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/OpenStandalone.ps1 -Profile v08_integration
```

Run Unreal operations sequentially, with Source/Config frozen during builds and
native checks. UE5.8.4 CL58902052, Win64 Development, MSVC14.44.35229 /
SDK10.0.26100.0. At most 4 compiler actions; B83 used 2 under memory pressure.
Regenerate only the active v08 map when needed with
`./Scripts/BootstrapAssets.ps1 -Mode Generate -RebuildV08Region`; preserve v07
and earlier assets. Native fixture wallet/item/knowledge/placement/clock setup
is explicitly diagnostic. Independent RPCs and actual movement are distinct
from normal earned progression, packaged distribution and human acceptance.

All E01–E48 and V07-D01–D08/V06-D01–D08 remain mapped and preserved. Human,
physical/rendered input, cold package/distribution, long sessions, full adverse
cross-feature races/privacy and normal progression journeys remain pending or
deferred as named. See the B83 report for actual final commands and evidence.
