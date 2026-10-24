# Continuation — B74 acquisition build; B75 next

24 October 2026. Read [B74 implementation](v07/B74_IMPLEMENTATION.md) for exact
supplies/recipes/work orders/quality/cache scope. B70–B73 are historical checkpoints.
Keep the deletion of `V07_TRANSITION.md`; this is the active handoff.

## Play and reproduce

```powershell
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/BootstrapAssets.ps1 -Mode Validate
python Scripts/SyncV07CombatData.py --check
python Scripts/SyncV07EquipmentData.py --check
python Scripts/SyncV07AcquisitionData.py --check
./Scripts/RunTests.ps1
./Scripts/RunAcquisitionCheck.ps1
./Scripts/RunAcquisitionNetworkCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunLoadoutCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunGeometryCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunTacticalCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunCharterCheck.ps1 -AuthorityOnly
```

Run Unreal operations sequentially; freeze Source/Config during each. UE5.8.4
CL58902052, Win64 Development, MSVC14.44.35229/SDK10.0.26100.0, compile cap4.
Each UPROPERTY owns one declaration. Commit authorized; no push requested.
Normal frontend remains V05; explicit v07 opens the native regional lobby.

Inventory has Supply, Gear craft and Ruins sections in v07. City supplies all28
priced items; West/East sell CircleII books60g; East Archive sells CircleIII90g,
two global paid creations per definition. Twelve supported passive gear effects
include Broker's Seal; Null Bell works. Other acquired items remain explicitly
labelled recipe/trade cargo until B75 effects/bindings exist. Unsupported books
have physical sources but cannot yet read/equip/cast their unfinished effects.

All12 exact recipes project final count/weight and use native held production.
Town4s, East advanced6s, HeartForge8s; six quiet seconds, stationary, body sight,
range120cm, fresh physical hold. Equipment must unbind first. East work orders
buy only missing immediate official inputs; never fabricate Toolkit/Core/Crystal,
catalysts or Fragment. No T4 order, disassembly, recursive production or XP.
Quality Standard→Fine consumes Toolkit+6g/4s; Fine→Masterwork consumes
Toolkit+Crystal+12g/6s at East. Same ID/endpoints remain; assembly output becomes
Standard. Only eligible weapon damage/flat armor/item barrier uses1.07/1.14 floor.
Bound Broker reduces only the equipment/quality fee by one, minimum one.

Three fixed caches have one key/catalyst/named capstone each, endowed once.
Quiet4s opening reveals the shared panel without transferring anything. Explicit
pickup checks each item and capacity; keys require3s. Temporary retained-region
service adapters are Ruin A(-1400,800), B(0,1050), C(1450,800), HeartForge at
East(-200,+220). Full authored POIs and guardian remain B76. Guardian drops require
an actual paid `GuardianDefeat` receipt; the old respawning Sentinel cannot mint
the Fragment/book. Heartbreaker normal acquisition therefore still needs B76.

## Immediate B75 work

1. Implement remaining27 item effects/bindings and14 spell effects from adopted
   contracts. Acquisition metadata does not mean delivered combat behavior.
   Preserve native B71–B74 targeting, status, geometry, MP, privacy and authority.
2. Extend `LDEquipmentCatalog` only as complete behavior becomes available.
   Attributes/capacity remain independent of Break. Craftsmanship modifies only
   weapon damage, armor and item barriers. Preserve exact endpoints, family/major
   locks and positive-direct-damage locks through transfer/assembly.
3. Add active-gear binding plus quick-reference checks, consumed/reusable semantics,
   ward/trap limits, reflect-once, grapple/blink geometry and counters. Owned
   `Quality`, `ReadyAt`, `BindingMask` and causal lineage now persist.
4. Supported spells become learnable from existing books only when their effects
   exist. Natural attributes qualify gates; knowledge/mastery/effective potency
   remain separate. One capstone maximum.
5. B76 implements actual guardian/world/towers, key/artifact leases and four normal
   routes with exact scheduled ordering. Preserve source budgets, fixed cache
   identities, individual pickups and the actual guardian defeat adapter.

## Evidence and boundaries

`docs/v07/evidence/B74/Summary.json` records final artifacts, source hashes,
transport and independent replays. The earned local two-owner lane grants no
diagnostic money/items/XP, but controls clock/placement/quiet/protection setup.
The independent-client network boundary lane labels diagnostic gold/Toolkit setup.
Neither certifies human travel or full sessions.

Public observations remain8 fields and GameState34. Production service IDs accept
all9 native adapters. New request fields are owner-to-server; offered quality is
deliberately visible only on voluntarily offered trade items.

The93 adopted spec files are not edited by B74. A user whitespace edit in
`docs/v07/09_VICTORY_AND_MATCH_STATE.md` predates this work and remains excluded
from the commit; the other92 match adoption bytes. Preserve that user edit.

Only the original source audit is Complete in FeatureLedger. Keep inherited
V06-D01–D08 and B77 gaps visible: full100/150 network/privacy, rendered/OS-scale/
physical input, cold package, normal1/4-player35-minute sessions, restarts/
performance and human review. Reuse native transactions and sanitized observations.
