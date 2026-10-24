# Continuation — B70 complete; B71 next

24 October 2026. B70 audited source, adopted the supplied v0.7/master revision7
without editing their original bytes, chose adapters and initialized the
working ledger. **C001 source/build audit complete;54 gameplay checks Not run.**
Read [B70 review](v07/B70_BASELINE_REVIEW.md) for inspected source/decisions and
the [transition index](V07_TRANSITION.md) for the contract/evidence links.

## Runnable baseline

Runtime remains B66 **`920c4aede4fab1338bbec4fc728ebcee14f56407`**. B67 `289501c`
closed v0.6 with verification deferrals. B70 adds audit/tooling/docs;
new v07 gameplay/catalogs/selectors remain implementation work.

```powershell
./Scripts/OpenStandalone.ps1
./Scripts/OpenStandalone.ps1 -NavigationCheckpoint
```

The first opens the frontend; the second directly opens the economy region with
`Profile=V05`. Host a private lobby, choose a legacy class, Ready and start.
Current rules:25 minutes, level5, four skills/four quick references,24 instances,
1–4locked humans and registered public Charter. The older22 October package
predates the economy implementation; no fresh package exists. Reserved
`v06_regression`/`v07_integration` selectors are B71 work. Unknown profile strings
can fall through regional map logic; do not launch v07 via an invented URL.

Fresh B70 [verification](v07/evidence/B70/Verification.json): pinned Editor build,
7 actions/60.94s; existing-asset Validate,28 packages/0errors; spec model931
assertions. [Audit](v07/evidence/B70/Audit.json):189 retained source hashes,
215 runtime input hashes,93 supplied-spec hashes and48 inherited acceptance rows.
Source/Config/Content/project unchanged from B66. No new gameplay pass is claimed.

## Immediate B71 work

1. Read [00 decisions](v07/00_DECISIONS_AND_MIGRATION.md),
   [02 action semantics](v07/02_COMBAT_RUNTIME_AND_EFFECTS.md),
   [11 authority](v07/11_AUTHORITY_NETWORKING_AND_UI.md),
   [16 import](v07/16_DATA_CONTRACT_AND_IMPORT.md) and
   [B70 adapters](v07/B70_ADAPTERS.json). Follow the ordered decisions in the
   [B70 review](v07/B70_BASELINE_REVIEW.md#adopted-implementation-decisions-and-dependency-order).
2. Isolate profiles, request/owner serialization and Content catalogs before
   shared formula changes. Preserve schema1 assets/economy/V05 regression.
   New build shape supports38 skill IDs/five slots; validate schema/profile/
   digest/bounds. Never mix contracts or switch during a live match.
3. Extend the actual combat director/dispatcher with contender MP, accepted
   windup/release, typed damage/barriers and core statuses/dispel/block. Start
   Arc Bolt/Anchor Bash/Purge Touch/Null Bell. Preserve economic zero-MP costs,
   score formulas, holds and persistent cooldowns.
4. Run affected short authority/resource checks. B72 geometry/control,
   B73 normalized development/gear/input/death, B74 acquisition, B75 full content,
   B76 world/four routes and B77 integrated evidence follow. B71–B73 is partial.

## Verification and work discipline

[Working ledger](v07/FeatureLedger.csv) tracks v07;
[inherited matrix](v07/evidence/B70/InheritedAcceptance.csv) carries E01–E48
without upgrading statuses. V06-D01–D08 stay open: independent clients/privacy/
latency, cold package, normal sessions, restart/performance, human review,
exhaustive races, physical/OS input and complete class Witness journeys.
B73 must exercise normalized scores natively; B70 arithmetic is not a normal
journey. Do not repeat B61–B67 long runs merely to begin B71.

Keep UE5.8.4 CL58902052, Win64 Development, MSVC14.44.35229/SDK10.0.26100.0.
Unreal operations sequential, Source/Config frozen during each operation,
build worker cap4. One UPROPERTY declaration per field. Extend privacy allowlists
and wire bounds deliberately; no replicated opponent build or client-computed
cost. Failed allocation spends nothing; combat/death precede noncombat/claims;
terminal state stops later commitments. User authorized committing; no push.
