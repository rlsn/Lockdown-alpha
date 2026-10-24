# Continuation — B73 tactical review build; B74 next

24 October 2026. Read [B73 implementation](v07/B73_IMPLEMENTATION.md) for exact
build/gear/input/preparation/death scope and evidence. B70–B72 are historical
checkpoints. Preserve all93 adopted spec files and the deletion of
`V07_TRANSITION.md`; this is the active handoff.

## Play and reproduce

```powershell
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/BootstrapAssets.ps1 -Mode Validate
python Scripts/SyncV07CombatData.py --check
python Scripts/SyncV07EquipmentData.py --check
./Scripts/RunTests.ps1
./Scripts/RunLoadoutCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunGeometryCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunTacticalCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunCharterCheck.ps1 -AuthorityOnly
```

Run Unreal operations sequentially; freeze Source/Config during each. UE5.8.4
CL58902052, Win64 Development, MSVC14.44.35229/SDK10.0.26100.0, compile cap4.
Each UPROPERTY owns one declaration. Commit authorized; no push requested.

Normal frontend still hosts V05. Explicit v07 opens the regional native lobby.
Ten circleI spell books remain Guild40g; advanced spell sources and all eleven
new passive gear supplies/assembly are B74. No free normal gear kit. Item effects
are implemented for eleven passive gear definitions plus Null Bell; fourteen new
spell effects are implemented; fourteen further effects remain B75. Unsupported
definitions stay unavailable.

Books teach R1 through a quiet3s read, including while naturally ineligible.
Station training/attributes/five skill slots commit after2s and use natural spell
gates. Body/weapon/feet/charm bindings and three actual quick references require
6 quiet seconds then a held2s button. Books are not combat references. Equipment
stays in the same24-slot bag; two-hand reservation counts once. Bag previews show
clipping/capacity and equipped state. V07 defaults Q/W/E/R/D and1/2/3, with separate
migrated preference file; preserve legacy preferences. Source data/native assets
must stay synchronized (`combat.json`, `equipment.json`, two native catalogs).
Generate refreshes the tactical asset only if stale and creates/refreshes the
new equipment asset; avoid baseline Regenerate.

V07 ordinary bound gear retains on death; spare gear/books/cargo/consumables drop.
Respawn endpoint uses the selected level/time formula once; protection3s, full
HP/MP, no supply grants and no cooldown reset. The current disconnect policy
retires identities and drops retained gear to avoid orphan holders; it does not
certify rejoin persistence. Future mandatory objective/relic drops remain tied
to their future implemented definitions. Eight-field observations/34 public fields
stay fixed; gear/build/inventory details remain owner-only.

## Immediate B74 work

1. Read item/acquisition/recipe/quality/data contracts against B73 runtime.
   Add paid sources for the eleven supported gear pieces and correct circleII/III
   spell books at trainers/archive; enforce per-definition CircleIII stock2.
   Use actual legal purchase sources and ledger burn/create transactions.
2. Implement assembly/work orders/branches/quality on exact physical instances
   and parent lineage. Equipped/reserved inputs must explicitly unbind first.
   Physical `BindingMask` and `ReadyAt` now exist; preserve identity/endpoints on
   upgrades/transfers and recalculate final count/weight/capacity before commit.
   Do not increase primary bonuses through craftsmanship or grant craft XP.
3. Capacity is natural STR plus bound bonuses; gear cannot qualify spell gates.
   Keep selected quality, fee discount and new passive/active functionality honest.
   Remaining28 item definitions and14 new spell effects belong to B75; do not
   import them as usable until their effects and sources exist.
4. Add normal purchase/branch/read/rank evidence. Existing B73 network evidence
   uses diagnostic grants, XP/funding/quiet/placement/clock and is not that journey.
5. B76 adds new world/four routes and exact scheduled ordering. Preserve the
   working public Charter, v07 damage/proximity claim semantics and legacy V05
   regression while extending common access/terminal ordering. B77 retains full
   rendered/physical/OS-scale/privacy/network/normal session/human acceptance.

## Evidence and boundaries

`docs/v07/evidence/B73/Summary.json` records final artifacts, source hashes,
transport measurements, ledger replays and93 unchanged specs. FeatureLedger is a
working scoped ledger; only the original source audit is Complete. B73 is the
B71–B73 tactical review build, not full v0.7. B70 frozen source-check output is
historical; do not rewrite it to match migrated runtime.

Keep inherited V06-D01–D08 gaps visible: full100/150 network/privacy, cold package,
normal1/4-player sessions, restarts/performance, human review, exhaustive races,
physical/OS input and normal class Witness journeys. Reuse current native helpers,
transaction authority and sanitized observations; do not build a replacement demo.
