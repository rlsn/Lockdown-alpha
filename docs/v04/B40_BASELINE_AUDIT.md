# B40 — verified Unreal baseline

19 October 2026. Starting revision: `451fcb5` (documentation preparation; runtime
source unchanged from the archived B36 checkpoint). The engine remains Unreal
5.8.3 CL 58210709, installed distribution, with MSVC 14.44.35229 and Windows SDK
10.0.26100.0. The editor closed normally before the non-Live-Coding builds.

Fresh verification completed before compiling the new character-development code.
Evidence paths below are relative to repository-root `Artifacts/`.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Toolchain preflight | Passed; supported compilers, engine, Windows and .NET Framework SDKs | `20261019-091610-666-Preflight/Environment.json` |
| Editor build | Passed | `20261019-091614-291-BuildEditor/BuildEditor.log` |
| Separate Game build | Passed | `20261019-091657-338-BuildGame/BuildGame.log` |
| Saved asset validation | Passed; existing 23-package migration baseline | `20261019-091757-053-Bootstrap-Validate/Bootstrap.log` |
| Native rules/schema | Passed; 8 tests | `20261019-091825-982-Automation/Report/index.json` |
| Movement/navigation | Passed; 7 checks | `20261019-091845-588-MovementCheck/Movement.json` |
| Separate authority and two restricted clients | Passed; Server 12 / A 29 / B 27 checks | `20261019-091858-303-ViewNetworkCheck/{Server,A,B}.json` |
| Combat/life world probe | Passed; 48 checks | `20261019-091926-539-CombatCheck/Combat.json` |
| Combat network, 150 ms configured RTT / 25 ms jitter | Passed; Server 9 / A 16 / B 19 checks | `20261019-091939-460-CombatNetwork-150-25/{Server,A,B}.json` |
| Objective world cases | Passed; Main 53, ExactDeadline 3, LateClaim 3, NoClaim 4, Withdraw 5 | `20261019-092033-717-ObjectiveCheck/*.json` |
| Objective network, 150 ms configured RTT / 25 ms jitter | Passed; Assault 15/21/18; Seal 15/19/20 | `20261019-092131-673-ObjectiveNetwork-150-25/{Assault,Seal}/{Server,A,B}.json` |
| Existing packaged build, fresh restricted-client run | Passed; Server 12 / A 29 / B 27 checks | `20261019-092455-718-Packaged-ViewNetworkCheck/{Server,A,B}.json` |
| Existing packaged build, fresh Seal network run at 150/25 ms | Passed; Server 15 / A 19 / B 20 checks | `20261019-092514-127-Packaged-ObjectiveNetwork-150-25/Seal/{Server,A,B}.json` |

The packaged regressions used the retained archive
`Artifacts/20261019-001732-992-PackageWin64/Archive/Windows`; they are fresh runs of
the v0.3 binary, not a newly cooked v0.4 package. Its source and payload provenance
remain in the [archived B36 report](../v03/implementation/TestReport.md).

## Retained systems and limits

The native movement, geometry, authority sample, private view relay, fog, combat,
life, real item ownership and Heart transactions are usable foundations. Keep the
focused C++ architecture and installed engine. No prerequisite platform rewrite or
new ability framework is needed.

The [migration ledger](../v03/implementation/MigrationLedger.csv) still inventories
full conquest, profession/crafting, economy, population and autonomous contender
ports as deferred. Seal acquisition and defence changes were diagnostic fixtures.
Native guest disconnect withdraws; authenticated resume is absent. A dedicated
Server package remains conditional on a compatible source engine. The packaged
regression authority was a headless listen process with two remote clients.

Human movement/aim/warning/usability comparison remains pending. The ten-reset and
ten-minute performance evidence from B36 was inspected, not rerun for B40. No WAN,
capacity, remote Git/LFS distribution or full human pacing claim is made.

The [baseline review](BASELINE_REVIEW.md) records the integer HP/damage, fixed fog
bounds, old action shortcuts, fixed objectives and login-budget risks. B41 begins
with private character state and a Ready/start roster boundary; B42–B47 remain the
ordered release work. Actual v0.4 acceptance must be recorded against its own build.
