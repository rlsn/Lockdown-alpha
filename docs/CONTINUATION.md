# Active continuation — v0.8 B82

Base: main at02687f1 (B81); B80 adoption aeffed2. B76 runtime ddf7be5 and
B77 closeout7d9d66d remain separately reproducible. B82 implementation and
validation scope live in [B82_IMPLEMENTATION](v08/B82_IMPLEMENTATION.md).
The normative [B80–B86 plan](v08/11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md),
original data/templates/archive/manifests and prior evidence remain frozen.
The working [FeatureLedger](v08/FeatureLedger.csv) tracks current engineering
acceptance; it does not rewrite original “Not run” specification templates.

## Current native slice

Explicit `?Profile=v08_integration` uses L_V08Region; explicit v07 still uses
L_V07Region. Default frontend/V05 remain separate. Both catalogs select their
own schema/digest, with active schema8 UFS payloads combat_b82/equipment_b82.
B80 and B81 JSONs are historical and must not be regenerated to hide migration.
SyncB82Data.py is the current checker; SyncB81Data.py checks its historical
payloads. TestV08Import isolates B80's retained enum prefixes in temporary
fixtures, while B82's checker validates current sparse native reservations.

B82 adds two static river floors with dry alternatives, native Wet entry/
100ms refresh/3s residual (Waders1.5s), source-bound Frozen30% slow (Treads15%),
and direct hostile Frost's fixed600ms Stun attempt / victim-wide6s readiness.
The contender endpoint survives death/return and dispel. Actual packet block,
life checks and existing guard/immunity precede the relevant effect. Surface
contact never stuns. Due applied Stun precedes native Heart completion.

The two-state cell slice uses200cm origin(-6000,-4000),16 cells/action,
64 distinct owner cells/256 match cells and bounded boundary evaluations.
Allocation checks precede release costs. Source life/owner/cast/creation/expiry
are private; committed surfaces survive caster death. Field Practice extends
new Frozen once, capped10s. Existing emitter budgets remain separate.

Frostline38 and Water Jet40 use reserved IDs; Firebolt39 remains disabled.
Schema8 ranks now have41 identities; schema7 remains38. B82 exposes29 learned
abilities/59 items/all9 presets, including the authored Arcanist teaching kit.
The five new items are Waders, Treads, Frost Prism, Water Flask and Frost
Capsule. Ember Line's v08 surface and other unsupported operations stay hidden.
All current clients need matching native modules; old executable compatibility
and a new reconnect mechanism are not claimed.

B81's six typed action refs, eleven bindings (head/torso/legs/feet/gloves/
main/off/four accessories), atomic drafts, source invalidation, item identities,
consumable clearing/death rules and absolute cooldowns remain the loadout path.
Trait is derived from immutable preset. Native input/HUD/preparation use the
same owner state and source definitions. Environment views contain authorized
appearance/pose only, not a canonical grid or hidden creator/life records.

## B83 boundary

1. Extend the bounded Wet/Frozen service to all six conditions plus finite Oil,
   and all ten reaction rows. Keep static river substrate outside cell budgets.
   Preserve the existing direct-Frost unit algorithm, endpoint and claim order.
2. Complete reservation/revalidation for delayed paid projectiles, overlap/
   overwrite ownership,512 transition evaluations/100ms, expiry-first overload
   behavior and complete damaging contact/tick attribution. No paid field may
   silently disappear or defer due combat/Heart work.
3. Integrate Steam/Smoke union occlusion, observation recomputation and bounded
   actual-mask previews. Preserve source privacy; never send the full grid or
   hidden owner IDs to make a client preview convenient.
4. Keep original source life for persistent fields through death, prevent reward
   duplication and respect protected bodies. Wards/traps/gates keep their own
   life-bound semantics. Avoid adding boss control immunities not declared by
   the existing category rules.
5. Preserve exact native event order. Before real authority movement, the
   movement component drains earlier events against the old pose; entry then
   applies Wet before same-boundary packets. Do not fabricate past trajectories
   from a new pose. Surface commands/contact follow direct packets.
6. Enable new catalog definitions only with real native operations, costs,
   acquisition and owner/client UI support. Preserve B80 numeric reservations,
   isolated v07 data/profile and frozen historical payloads. B84 owns catalog
   completion; B85 owns complete world integration; B86 owns usability/release.

## Commands and evidence limits

```powershell
python -B Scripts/SyncB82Data.py --check
python -B Scripts/TestV08Import.py
python -B docs/v08/tools/check_spec.py
./Scripts/BuildEditor.ps1 -MaxParallelActions 1
./Scripts/BootstrapAssets.ps1 -Mode Validate
./Scripts/RunTests.ps1
./Scripts/RunB82Check.ps1 -RoundTripMs 100 -JitterMs 10 -TimeoutSeconds 180
./Scripts/RunB81Check.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/OpenStandalone.ps1 -Profile v08_integration
```

Run Unreal operations sequentially, with Source/Config frozen during native
build/test execution. UE5.8.4 CL58902052, Win64 Development,
MSVC14.44.35229 / SDK10.0.26100.0, at most4 compiler actions. B82 used one worker
under local memory pressure. Native fixtures label diagnostic wallet/item/
placement/clock setup; independent remote RPCs and actual movement are distinct
from normal earned progression or human acceptance. See the final B82 report
for completed checks and portable reports.

All E01–E48 and V07-D01–D08/V06-D01–D08 remain mapped and preserved. Human,
physical/rendered input matrices, cold package/distribution, long sessions,
full cross-feature races/privacy and normal progression journeys remain
pending/deferred as named. Focused engineering tests do not waive them.
