# v0.4 implementation record

Implementation started 19 October 2026 from `451fcb5`. This is an incremental
development record, not acceptance of the complete v0.4 release.

B41 runtime checkpoint: `3a1f389a9adda2a827fc01cbbf88d44c19590bdd`.
Current preparation and book changes: [B43 checkpoint](B43_IMPLEMENTATION.md).
The [B42 execution checkpoint](B42_IMPLEMENTATION.md) remains historical evidence
for `4306e46`; its clean-checkout builds, 24 assets and 12 rules passed.

## Milestones

- B40: baseline verification passed; see [actual evidence](B40_BASELINE_AUDIT.md).
- B41: implemented and engineering-verified, including a cooked Windows checkpoint.
- B42: equipped execution, native scaled Step, Ambush, Spark, Patch, held utility
  dispatch, private cooldowns and mixed hotbar implemented. Utility rewards and
  wallets remain B44; the trained NPC content remains B45.
- B43: private ghost preview, atomic held station commit, class/rank spending,
  permanent subclass choice and unique physical books implemented. An automated
  ordinary courier-funded Rogue learns Spark and prepares a mixed four-slot build.
  See its checkpoint record for native/cooked evidence and fixture boundaries.
- B44–B47: not implemented or accepted. Charter services, wallets/theft, the
  connected region, earned fabrication and full release verification remain open.

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
cannot bypass equipment. Q/Space now resolve owned slots through B42's shared
execution path. The legacy profile retains its explicit fixed-kit regressions.

## Verification boundary

The B41/B42 probes label grants, helper builds, teleports and capability callbacks
as engineering fixtures. B43 separately verifies actual station allocation, books,
subclass choice and an ordinary courier-funded cross-class preparation run.
Its authority/network setup grants remain fixtures, and its ordinary run is
automated local practice. None demonstrates earned Heart acquisition.
The new region, expanded fog bounds, shared trained NPC, wallet theft and real
fabrication remain later milestone work. Current play still uses migration-map
geometry and courier flow; it is a development checkpoint.

Native rule tests cover starting budgets, derived and skill arithmetic, threshold/cap
awards and rejected/cumulative build plans. World/network probes exercise the
actual contender records, lobby RPCs, movement limits, fractional impact, death,
private replication and world-reset boundaries. Local screenshots are engineering
layout inspection, not human usability acceptance.

## B41 executed verification

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
tests do not establish acceptance of B44–B47. B42/B43 executed cases are indexed
separately and update only the applicable engineering rows below.

| ID | Status | Evidence or remaining work |
| --- | --- | --- |
| C01 | Passed | B40 actual source/toolchain/build and native/packaged baseline audit |
| C02 | Passed | Class lobby, shared body type and private presets; no hero or primary-growth fields |
| C03 | Passed | Saved catalog, equal-budget native start tests and class-choice transactions |
| C04 | Passed | Threshold/cap tests, actual contender XP and independent client point awards |
| C05 | Passed | B43 authority/UI preview and atomic held spends; rejected overspend/caps/stale plans |
| C06 | Passed | Exact derived values and actual fractional authority impact |
| C07 | Passed | B43 held recalculation and ordinary HP 95-to-100 maximum change preserve current 95 |
| C08 | Passed | B43 held class-list learning; cross-class book learning and subsequent SP ranking |
| C09 | Passed | Actual cumulative learning/rank purchases and insufficient two-point rank-three rejection |
| C10 | Passed | Level/class/permanence/no-free-skill transaction cases; all six effect formulas in native rules; human diversity pending |
| C11 | Passed | Private learned-library/equipment UI; six known skills and four distinct shared slots; B42 activation guards |
| C12 | Passed | B42 slot/revision checks, shared queue, rejected legacy/context bypasses and common NPC entry path |
| C13 | Passed | B43 two-second Guild/West transactions; release/stale/movement/damage/revision/range/action guards; ordinary panel commit |
| C14 | Passed | B42 cast/death/new-body retention plus B43 actual recent Step swap and rank/reading plans preserve absolute endpoints |
| C15 | Passed | B43 forty-gold purchase; unique instance; capacity/funds/stock/duplicate rejection; delayed owning RPCs |
| C16 | Passed | Ordinary courier-funded Rogue reads Spark at rank one; no auto-equip/SP/XP; known book retained |
| C17 | Passed | Actual three-second read; deadline damage, ownership loss and release preserve book; death drops and opaque pickup; idempotent withdrawal drop |
| C18 | Passed | STR-scaled real melee and retained cadence/geometry regression path |
| C19 | Passed | B42 bounded DEX/rank math, real remote 380 cm travel and rank-three wall collision; retained native ramp/ledge/tree checks |
| C20 | Passed | B42 scaled fractional Ambush, warning/exposure, rear/target-history counterplay and interruption |
| C21 | Passed | B42 locked one-body cast, fractional snapshot, range/sight/protection rechecks, subclass arithmetic and delayed private warning |
| C22 | Passed | B42 capped snapshot healing, full-HP rejection, held refresh/release/staleness/movement and deadline damage ordering |
| C23 | Not run | Four Charter services and Witness pending |
| C24 | Not run | Spark and utility dispatch geometry verified; real Charter/theft/sabotage targets remain B44–B45 |
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
| C41 | Passed | Retained B41/B42 life/build/cooldown checks plus B43 acquired knowledge, physical drops and stale old-life book requests |
| C42 | Not run | B41–B43 builds/skills/books use owner-only state; eight-field schema and delayed clients pass; future wallet/DC domains remain B44–B45 |
| C43 | Not run | B42 trained-build NPC Patch fixture passes shared slot/rank/channel/cooldown rules; authored trained NPC remains B45 |
| C44 | Not run | Lobby lock and ten B43 resets cover books/channels/queues; future wallet/receipt domains and authenticated resume remain pending/deferred |
| C45 | Not run | B43 builds/assets/cooked clients supplement retained delivery evidence; full V04 scenarios pending |
| C46 | Not run | B42 skill and B43 book/point cases pass at native 100/150 ms and cooked 150 ms; random theft cases remain B44 |
| C47 | Not run | Ten B43 resets pass; normal 25-minute and separate 90-minute cases pending |
| C48 | Not run | Honest checkpoint ledger/report supplied; full release/human evidence pending |

No human matches, build-diversity study, normal earned acquisition, long-session
pacing or 90-minute stability run occurred. Dedicated packaging and remote Git/LFS
distribution remain conditional/unverified. Next implementation milestone: B44.
