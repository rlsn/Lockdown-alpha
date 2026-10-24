# Continuation — B72 geometry/control; B73 next

24 October 2026. [B72 implementation](v07/B72_IMPLEMENTATION.md) records delivered
behavior, normal sources, diagnostic-only source boundaries and verification.
B71/B70 are historical checkpoints. The original93 adopted spec files remain
byte-preserved. Do not recreate the deleted `V07_TRANSITION.md`.

## Play and reproduce

```powershell
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/BootstrapAssets.ps1 -Mode Validate
python Scripts/SyncV07CombatData.py --check
./Scripts/RunTests.ps1
./Scripts/RunGeometryCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunTacticalCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunCharterCheck.ps1 -AuthorityOnly
```

Unreal operations are sequential; freeze Source/Config during every operation.
UE5.8.4 CL58902052, Win64 Development, MSVC14.44.35229/SDK10.0.26100.0,
compile cap4. Every UPROPERTY has one declaration. No push requested.

Normal frontend remains V05. Explicit v07 opens the native regional lobby:
choose a retained class, Ready and start. Earn gold through retained work. Guild
books now include the four B71 spells plus Veil Step, Counterpulse, Farwatch Ward,
Wire Snare, Smoke Veil and Seer's Flare,40g each. Quiet-read, then equip at a
station. No free normal tactical kit. CircleII/III effects exist for the B72
fixtures; correct trainer/archive sources and finite CircleIII stock are B74.

Ground/cone spells arm a static ground click, including fog. Unit spells require
current authorized observation. Purge/Remedy press-again selects self. Bag Items
has current-life aid consent ON/OFF; external Remedy requires it. Null Bell
remains8g at Official Supply with three usable quick references. Source JSON is
now `Content/Lockdown/Data/V07/combat.json`; the old B71 file is historical.
`SyncV07CombatData.py` updates current subset plus native checksum contract.
Generate mode refreshes a stale tactical asset only; avoid baseline Regenerate.

## Immediate B73 work

1. Read progression/equipment/input/death and data contracts against actual B72
   code. Keep legacy/V05 adapters explicit.
2. Complete natural/effective attributes, declared potency, six gear bindings,
   two-hand conflicts, instance/weight/count semantics and stat clipping. Break
   must query only explicitly tagged breakable passives. Natural gate OR/AND
   checks already exist for delivered learning/casts; extend equipment/station
   previews and full progression without using gear to satisfy natural gates.
3. Migrate saved controls to Q/W/E/R/D and1/2/3, resolve conflicts and remove old
   quick4 entry points. Current fifth slot has mouse activation; backing quick
   array still has four legacy cells. Preserve compatible existing preferences.
4. Finish death/retention/preparation and owner item/family endpoint persistence,
   source cleanup and native UI evidence. Major-protection owner endpoint and
   current-life consent are present. No new source may be advertised inert.
5. B74 acquisition, B75 remaining14 ability/39 item definitions, B76 world/four
   routes/exact late-event ordering and B77 integrated/human evidence remain.

## Evidence and constraints

`docs/v07/evidence/B72/Summary.json` pins final gates and source manifest.
The new authority/two-client fixture uses real RPCs with diagnostic learned ranks,
status/access/placement and a controlled collision wall. It is not a normal
journey. V07 claims now survive ordinary damage/proximity; actual displacement,
explicit interrupt and hard control cancel. Retained Charter access stays in
place until B76. Do not claim four-route or exact stalled-tick/tie guarantees.

Eight-field observations append a field presentation kind with no owner/source
identity; ward-source count has an explicit bounded extension to8. Public
GameState remains34 fields. MP/status/consent/protection endpoints are owner-only.
Smoke and detection use shared wall/elevation/union geometry. Failed field
allocation/owner limit spends nothing. Future targeting must recheck current
observation, not just a cached handle.

[Working ledger](v07/FeatureLedger.csv) remains scoped partial. Inherited
V06-D01–D08 stay open: broad100/150 network/privacy, cold package, normal1/4-player
sessions, restarts/performance, human review, exhaustive races, physical/OS input
and complete class Witness journeys. B70's frozen baseline source checker is
historical; do not rewrite its report to match migrated source.
