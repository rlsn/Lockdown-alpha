# Continuation — B76 native world; B77 acceptance next

25 October 2026. Read [B76 implementation](v07/B76_IMPLEMENTATION.md) and its
[portable evidence](v07/evidence/B76/Summary.json) for runtime and verification
scope. B70–B75 remain historical records. Keep `V07_TRANSITION.md` deleted;
this is the active handoff.

## Play and reproduce

```powershell
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
./Scripts/BuildEditor.ps1 -MaxParallelActions 4 -Gather
./Scripts/BootstrapAssets.ps1 -Mode Validate
python Scripts/SyncV07CombatData.py --check
python Scripts/SyncV07EquipmentData.py --check
python Scripts/SyncV07AcquisitionData.py --check
python Scripts/SyncV07WorldData.py --check
./Scripts/RunTests.ps1
./Scripts/RunWorldCheck.ps1 -Route All
./Scripts/RunWorldBoundaryCheck.ps1
./Scripts/RunWorldNetworkCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunWorldNetworkCheck.ps1 -RoundTripMs 150 -JitterMs 10
./Scripts/RunContentCheck.ps1
./Scripts/RunContentNetworkCheck.ps1 -RoundTripMs 100 -JitterMs 10
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
Default frontend/V05 regression remain on the retained map. Explicit v07 launches
`L_V07Region` with imported `DA_WorldCatalog` and native regional assets.

## Current runtime

B75's28 spells/40 items, six extra starters and B74's paid supplies/12 recipes,
finite caches, quality, natural gates, binding and quick references remain.
The new world adds three bounded camp pools, guardian, three vulnerable towers,
shrines, ruins and forge. Tower death removes sight and its native obstruction;
all three down grants permanent global siege. Guardian and towers issue physical
contestable drops with atomic finite reward receipts.

Inspect the antechamber: Context4s activates three physical keys;
Shift+Context6s consumes an owned Heartbreaker. Both publicly warn30s, then grant
60s to that life only. Key loss revokes permanently; the90s owner and instance
reuse endpoints survive transfer/re-pickup. Artifact death cancels without refund.
Inspect dropped keys: Context3s within80cm after6s quiet. Shrines: Context4s after
6s quiet, full HP/MP and90s reuse. World interaction IDs10–14 preserve old ID9.

Retained public Charter and siege/private routes share one10s stationary Heart
hold. Nonlethal damage and rival proximity preserve it; actual displacement,
stun/hex, explicit interrupt, death, release, lost access and own actions cancel.
Exact-close/deadline completion is inclusive; equal completions use acceptance
ordinal. Timestamp draining resolves due combat before completion and produces
one terminal result. Native movement is not historically resimulated; ambiguous
changed pose conservatively cancels an older stationary hold.

Old diagnostic-map ruin/forge adapters remain for prior regression scripts.
The new region reads authored positions from the closed world catalog. Camp
placement keeps the covered economic road and service counters viable without
requiring a kill. Declared encounter stats and reward counts are unchanged.

## Immediate B77 work

1. Complete the cross-feature100/150ms/privacy matrix, including progression,
   delayed source attribution, disconnected/dead hitters and adversarial travel.
2. Verify rendered and physical input, inspection/immunity readability, minimap,
   resolutions, OS/text scales and full human route/interference tasks.
3. Run cold cook/package and normal1/4-player sessions, restart/performance and
   inherited V06-D01–D08. Record actual results; do not relabel focused fixtures.
4. Reconcile the working ledger/acceptance matrix and delivery report against
   portable native logs. No full v0.7 release acceptance is claimed by B76.

## Evidence boundaries and preservation

Normal route demos use native movement/AI/combat, paid acquisition and real
elapsed holds with diagnostic grants disabled. They use automated commands and
fixed native20Hz delta; they are not human input, remote sessions or full pacing.
Boundary and dedicated network tests explicitly label source/placement/clock
fixtures. They cannot substitute for the normal route demonstrations. Public
observation remains8 fields; GameState adds only the anonymous four-fact breach
array (35 reflected public fields total).

The93 adopted spec files are preserved. A pre-existing user edit in
`v07/09_VICTORY_AND_MATCH_STATE.md` stays excluded; the other92 match adoption.
The old B56 Seal-trade fixture is incompatible with V05 economy. Do not enable
diagnostic Seal grants to force it through; B74's earned Masterwork trade/return
remains the current transaction regression.
