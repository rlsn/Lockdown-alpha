# v0.4 implementation record

Implementation started 19 October 2026 from `451fcb5`. This is an incremental
development record, not acceptance of the complete v0.4 release.

Runtime checkpoint: `3a1f389a9adda2a827fc01cbbf88d44c19590bdd`.

## Milestones

- B40: baseline verification passed; see [actual evidence](B40_BASELINE_AUDIT.md).
- B41: implemented and engineering-verified, including a cooked Windows checkpoint.
- B42–B47: not implemented or accepted. Preparation arithmetic exists as a tested
  helper, but the held station transaction and UI remain B43 work.

## Implemented foundation

The saved `DA_CharacterCatalog` imports schema-one tuning from the JSON companion,
records its SHA-256, validates class references and rejects changed native formula
or progression contracts. Runtime loads the asset without reading `docs/`.
The existing 23 migration packages are retained; this adds one catalog package.

Private contender state now includes starting class, six attributes, character
level, point balances, learned ranks, four slots, subclass field, build revision
and absolute skill cooldown storage. Class presets have equal 60-point budgets
and two equipped rank-one skills. Character XP caps at 420 with once-only rewards.

The native lobby accepts Laborer, Rogue or Scholar and Ready. The lobby host can
start only when every connected contender is Ready, with 1–4 contenders. One is
practice. Gameplay is blocked before start; the 1500-second deadline begins at
start. New guests and class changes are rejected after start. Public lobby state
contains only profile, started flag and aggregate connected/ready counts.

HP and combat damage use doubles throughout owner state and combat, including
ward damage. Native walk/sprint limits derive from the owner's DEX. Basic melee
snapshots STR damage at acceptance. Derived maximum-HP changes clamp without
healing; death retains build state, uses the new bounded delay/15 percent loss and
returns a fresh body with full derived HP. Existing movement prediction and
simultaneous combat ordering remain in place.

The frontend hosts with `?Profile=V04`. Opening the migration map without that
option is the explicit legacy profile used by the existing regressions. Migration
fixtures are hidden/disabled in the v0.4 profile. Global Q/Space skill activation
is rejected there until the equipped-skill path is integrated in B42.

## Verification boundary

The development probe labels its XP grants, allocated-build helper and teleports
as test fixtures. They do not demonstrate normal progression, station allocation,
books, subclass selection, mixed skill activation or earned Heart acquisition.
The new region, expanded fog bounds, shared trained NPC, wallet theft and real
fabrication remain later milestone work. Current play still uses migration-map
geometry and courier flow; it is a development checkpoint.

Native rule tests cover starting budgets, derived arithmetic, threshold/cap
awards and rejected/cumulative build plans. World/network probes exercise the
actual contender records, lobby RPCs, movement limits, fractional impact, death,
private replication and world-reset boundaries. Local screenshots are engineering
layout inspection, not human usability acceptance.

## Executed verification

All paths below are relative to repository-root `Artifacts/`. The engine remains
5.8.3 CL 58210709. The package was built before this checkpoint commit; its manifest
records the actual source revision/dirty flag and source/payload hashes.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Final Editor build | Passed | `20261019-095203-571-BuildEditor/BuildEditor.log` |
| Game build/cook/stage/archive | Passed; Win64 Development, file-backed cook and IoStore/pak | `20261019-095301-110-PackageWin64/{Package.log,PackageManifest.json}` |
| Saved assets/catalog | Passed; 24 packages, matching schema/checksum | `20261019-095245-616-Bootstrap-Validate/Bootstrap.log` |
| Native rule/schema suite | Passed; 11 tests | `20261019-094327-027-Automation/Report/index.json` |
| Native authority character probe | Passed; 24 checks | `20261019-095231-575-DevelopmentCheck/Local.json` |
| Native two-client development, configured 100 ms RTT / 25 ms jitter | Passed; Server 6 / A 6 / B 5 | `20261019-094820-887-DevelopmentNetwork-100-25/{Server,A,B}.json` |
| Native two-client development, configured 150 ms RTT / 25 ms jitter | Passed; Server 6 / A 5 / B 6 | `20261019-094403-565-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Ten v0.4 world resets | Passed; 42 checks | `20261019-094844-461-DevelopmentReset/Reset.json` |
| Packaged authority character probe | Passed; 24 checks | `20261019-095434-238-Packaged-DevelopmentCheck/Local.json` |
| Packaged two-client development, configured 150 ms RTT / 25 ms jitter | Passed; Server 6 / A 5 / B 6 | `20261019-095437-608-Packaged-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Packaged ten v0.4 resets | Passed; 42 checks | `20261019-095450-945-Packaged-DevelopmentReset/Reset.json` |
| UI layout inspection | Captured and visually reviewed at 720p/1080p; packaged 720p also reviewed | `20261019-094427-171-DevelopmentUI-720/Lobby.png`, `20261019-094451-220-DevelopmentUI-1080/Lobby.png`, `20261019-095501-208-Packaged-DevelopmentUI-720/Lobby.png` |
| Legacy native movement | Passed; 7 checks | `20261019-094905-480-MovementCheck/Movement.json` |
| Legacy native combat/life | Passed; 48 checks | `20261019-094533-368-CombatCheck/Combat.json` |
| Legacy native objectives | Passed; Main 53 / ExactDeadline 3 / LateClaim 3 / NoClaim 4 / Withdraw 5 | `20261019-094546-482-ObjectiveCheck/*.json` |
| Legacy private-view network | Passed; Server 12 / A 29 / B 27 | `20261019-094649-866-ViewNetworkCheck/{Server,A,B}.json` |
| Legacy ten world resets | Passed; 84 checks | `20261019-094512-276-ResetCheck/Reset.json` |
| Fresh local clone at `3a1f389` | Passed; 24 LFS assets, preflight, cold Editor/Game builds, Bootstrap Validate and 11 rules; checkout remained clean | `20261019-100904-002-CleanCheckout/Result.json`; logs under `Checkout/Artifacts/` |

The packaged echo medians were 183.04/182.98 ms from 25 samples per remote client.
This verifies actual emulated native transport, not WAN conditions or a 250 ms p95
action-acknowledgement claim. Its authority is a headless listen world with a
diagnostic local connection and two remote clients, not a packaged dedicated
Server target or a four-human capacity measurement.

The clean-clone check copied no project Binaries/Intermediate files. It reused
this machine's installed engine/shared caches and hydrated assets from the local
LFS object cache. It verifies local reproducibility, not remote Git/LFS delivery.

Archive: `Artifacts/20261019-095301-110-PackageWin64/Archive/Windows`.
All 72 current source/config files and seven archived payload files match that
package's manifest hashes. Documentation/provenance checks are recorded in
`Artifacts/B41DocumentationAndProvenance.json`.
Open its `Lockdown.exe` or use `Scripts/OpenPackaged.ps1 -PackageRoot '<archive>'`,
then Host, choose a class, Ready and Start. The complete earned objective route is
unavailable in this checkpoint; it is not a normal-rule acquisition demonstration.

## Corrections found during verification

Ward placement used a collision-channel overlap that could treat a nearby
humanoid as static ground obstruction. It now queries static object types, and
humanoid capsules explicitly ignore the ground-aim and vision trace channels.
A direct nearby-resident check and all ten legacy resets pass. Static walls,
trees, ground and movement geometry remain covered by the retained probes.

The first 720p lobby capture exposed an undersized panel and low-contrast buttons;
both target layouts were corrected and inspected. Catalog hashing uses the
engine's bundled OpenSSL because the generic platform SHA-256 entry point asserts
on this installed engine. An initial network completion check treated expected
final client disconnects as failures; that harness ordering was fixed. The melee
fixture now isolates its target so a legitimate nearest-target interception cannot
invalidate the damage assertion. Failed runs remain in `Artifacts/` and are
excluded from passing results above.

## Release acceptance status

Passed rows cover the stated engineering case. Not run includes partial foundation
evidence when its full v0.4 transaction/UI/scenario is still absent. Checkpoint
tests do not establish acceptance of B42–B47.

| ID | Status | Evidence or remaining work |
| --- | --- | --- |
| C01 | Passed | B40 actual source/toolchain/build and native/packaged baseline audit |
| C02 | Passed | Class lobby, shared body type and private presets; no hero or primary-growth fields |
| C03 | Passed | Saved catalog, equal-budget native start tests and class-choice transactions |
| C04 | Passed | Threshold/cap tests, actual contender XP and independent client point awards |
| C05 | Not run | Pure plan validates spends; held allocation transaction/UI remains B43 |
| C06 | Passed | Exact derived values and actual fractional authority impact |
| C07 | Not run | World recalculation does not heal; full rank/subclass/preparation flows pending |
| C08 | Not run | Class-learning helper exists; actual learning transaction pending |
| C09 | Not run | Cumulative rank costs tested as helpers; actual purchase pending |
| C10 | Not run | Subclass state/helpers exist; selection and all effects need integration |
| C11 | Not run | Four-slot storage/plan constraints exist; library/hotbar activation pending |
| C12 | Not run | Legacy Q/Space disabled in V04; nine-skill entry points need B42 |
| C13 | Not run | Held preparation station transaction/UI pending |
| C14 | Not run | Absolute cooldown storage survives death; active skill/swap path pending |
| C15 | Not run | Book purchase/inventory pending |
| C16 | Not run | Real cross-class reading pending |
| C17 | Not run | Physical book read/drop lifecycle pending |
| C18 | Passed | STR-scaled real melee and retained cadence/geometry regression path |
| C19 | Not run | Legacy native dash retained; equipped Step scaling pending |
| C20 | Not run | Legacy response rules retained; equipped Ambush integration pending |
| C21 | Not run | Spark activation, targeting and private cues pending |
| C22 | Not run | Patch activation, healing and interruption pending |
| C23 | Not run | Four Charter services and Witness pending |
| C24 | Not run | Legacy geometry passes; new spell/check/theft authorization pending |
| C25 | Not run | Theft helper exists; B44 enumeration/transaction evidence pending |
| C26 | Not run | One-draw theft RNG/receipt path pending |
| C27 | Not run | Stable wallet-pair retry windows pending |
| C28 | Not run | Atomic finite wallet transfer pending |
| C29 | Not run | Theft Alert/exposure consequences pending |
| C30 | Not run | Persistent resident wallets pending |
| C31 | Not run | Region graph, authored routes and alternate topology pending |
| C32 | Not run | Traversal and first-visit receipts pending |
| C33 | Not run | Preview pays 45 XP; alternating towns/eight-payment budget pending |
| C34 | Not run | Sentinel encounter and bounded reward credit pending |
| C35 | Not run | Normal peaceful level-five route needs region, visits and work budgets |
| C36 | Not run | Real Analyze sabotage pending |
| C37 | Not run | Earned fabrication pending; normal fixtures disabled |
| C38 | Not run | Normal zero-kill earned acquisition pending |
| C39 | Not run | Legacy recovery retained; earned-drop scenario pending |
| C40 | Not run | Delay/loss arithmetic and death/duplicate return pass; new withdrawal case pending |
| C41 | Not run | B41 retained-state/new-life probe passes; acquired rank/book/subclass flows need B43 |
| C42 | Not run | B41 private-build/schema/client checks pass; new skill/cue/target-state audit pending |
| C43 | Not run | Shared-rule trained NPC pending |
| C44 | Not run | Lobby lock and ten B41 resets pass; inventory/wallet/receipt domains pending |
| C45 | Not run | B41 builds/assets/cooked clients pass; full V04 delivery scenarios pending |
| C46 | Not run | B41 native 100/150 ms and packaged 150 ms cases pass; skill/spend/roll cases pending |
| C47 | Not run | Ten B41 resets pass; normal 25-minute and separate 90-minute cases pending |
| C48 | Not run | Honest checkpoint ledger/report supplied; full release/human evidence pending |

No human matches, build-diversity study, normal earned acquisition, long-session
pacing or 90-minute stability run occurred. Dedicated packaging and remote Git/LFS
distribution remain conditional/unverified. Next implementation milestone: B42.
