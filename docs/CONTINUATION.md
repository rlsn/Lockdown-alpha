# Continuation — B75 tactical content; B76 next

25 October 2026. Read [B75 implementation](v07/B75_IMPLEMENTATION.md) for the
complete effect, channel, cooldown and adapter details. B70–B74 are historical
records. Keep `V07_TRANSITION.md` deleted; this is the active handoff.

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
Each UPROPERTY owns one declaration. Commit is authorized; no push requested.
Default frontend still uses V05; explicit v07 opens the native tactical lobby.

## Current runtime

All28 spell/40 item definitions have effects. Six additional53-point starters
grant two adopted skills and no ward/item/gold. The Guild exposes all16 Circle-I
plus four legacy books at40g. Five shared skills retain natural
gates and the one-capstone limit. Equipment actives require binding plus one
of three quick references; consumables require a reference. Use the reference
to aim/activate or hold Mana/Recall. Inspect a gate/scramble then hold Context.

Wire3 carries bounded two-target/two-point/item revisions; wire2 single targets
remain readable. Server authority rechecks life, build, geometry and resources.
Item families persist after consumption and merge transferred instance endpoints.
Major protections share8s activation, with definition-specific longer reuse.
Blink/Rift direct damage lock3s persists on the contender. Ward/trap charges
remain one ledger source until retired. Death clears persistent fields/pairs;
bound Tower Relic always drops. Ordinary bound equipment retains.

B74 supplies/books,12 recipes, East work orders, same-ID quality and finite caches
remain. City sells28 priced items; CircleII60g at West/East; CircleIII90g at East
with two global copies per definition. Town/East/HeartForge holds4/6/8s after six
quiet seconds. Work orders never fabricate Toolkit/Core/Crystal/catalyst/Fragment
or T4. Assembly outputs Standard. Eligible fields alone use1.07/1.14 floor.

Retained-region adapters: Ruin A(-1400,800), B(0,1050), C(1450,800), HeartForge
at East(-200,+220), Cliffwalkers link derived from public Plateau geometry.
Actual guardian drops require a paid `GuardianDefeat` receipt and finite budget;
the old Sentinel cannot mint Fragment/book. The complete world remains B76.

## Immediate B76 work

1. Implement adopted camps/ruins/towers, guardian, shrines/forge and shortcuts in
   native geometry. Preserve finite physical sources and reward identities.
2. Connect key/artifact leases, global siege and retained public Charter to one
   common claim/terminal ordering path. Rivals retain entry/interference rights.
3. Demonstrate four routes through normal acquisition; add exact endpoint and
   concurrent winner checks. Keep fixture cases separately labelled.
4. Preserve B71–B75 combat, cooldown/source lifecycle, privacy, geometry, economy,
   quality and ownership regressions while integrating world events.

## Evidence and boundaries

`v07/evidence/B75/Summary.json` records raw native logs, source/import hashes and
independent ledger replays. The all-content effect lane explicitly grants
items/build/resources and controls clock/placement. B75 networking uses a
server and two independent clients. B74's earned acquisition lane grants no
money/items/XP but controls time/placement/quiet/protection. None proves human
travel or full sessions. Public observation remains8 fields and GameState34.

B76 world/route and B77 full100/150ms/privacy, physical/rendered input, OS scale,
cold package, normal1/4-player35-minute, restart/performance and human acceptance
remain open with inherited V06-D01–D08. No new package/full release is claimed.
The93 adopted spec files are preserved. A pre-existing user whitespace edit in
`v07/09_VICTORY_AND_MATCH_STATE.md` stays excluded; the other92 match adoption.
The old B56 Seal-trade fixture is incompatible with V05 economy. Do not enable
diagnostic Seal grants to force it through; use B74's earned Masterwork trade
and return as the current transaction regression.
