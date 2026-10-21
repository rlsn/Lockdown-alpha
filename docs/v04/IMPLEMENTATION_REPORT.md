# v0.4 implementation record

Implementation started 19 October 2026 from `451fcb5`. This is an incremental
development record, not acceptance of the complete v0.4 release.

B41 runtime checkpoint: `3a1f389a9adda2a827fc01cbbf88d44c19590bdd`.
Current earned acquisition changes: [B46 checkpoint](B46_IMPLEMENTATION.md).
Current checkpoint: [B47 record](B47_IMPLEMENTATION.md), core checks recorded.
Runtime commit: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`. The ordinary 25-minute
run passed. Full stress validation is deferred by user direction; a fresh B47
cold checkout and actual human review remain pending for the next version.
The [review guide](B47_REVIEW_GUIDE.md) records the human process.
B46 runtime checkpoint: `1b69d6324045e35b93a31d4b1207060e430ae47b`.
A fresh local checkout passed cold Editor/Game builds, 28-asset validation and
15 rules without tracked changes; the B46 record indexes the result and local
engine/cache/LFS boundary. That historical B46 checkout does not establish a
fresh cold B47 build. B47 graphics and duration results are indexed separately.
Region and progression history: [B45 checkpoint](B45_IMPLEMENTATION.md).
B45 runtime checkpoint: `35b02b88ca20df207475af17901f0200d7a8d65f`.
A fresh local checkout passed cold Editor/Game builds, 28-asset validation and
15 rules without tracked changes; the B45 record indexes its local-cache/LFS
boundary and the outstanding rendering-performance issue.
World-check and theft history: [B44 checkpoint](B44_IMPLEMENTATION.md).
B44 runtime checkpoint: `6356af102d54cf06ca05594fba2282dc20a7ee28`.
A fresh local checkout passed cold Editor/Game builds, 24-asset validation and
14 rules without modifying tracked files; the B44 record indexes the result and
local cache/LFS boundary.
Preparation and book history: [B43 checkpoint](B43_IMPLEMENTATION.md).
The [B42 execution checkpoint](B42_IMPLEMENTATION.md) remains historical evidence
for `4306e46`; its clean-checkout builds, 24 assets and 12 rules passed.
B43 runtime checkpoint: `f5da328`. A fresh local checkout passed cold Editor/Game
builds, 24-asset validation and 13 rules without modifying its tracked checkout;
the B43 record indexes its result and local cache/LFS boundary.

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
- B44: four equipped Charter alternatives, once-only private Witness, persistent
  humanoid wallets and transactional Pickpocket implemented. Its checkpoint
  indexes native/cooked tests, delayed clients, ordinary input and reset evidence.
- B45: connected region, four/five-node saved topologies, real bounded courier and
  traversal rewards, lethal-batch sentinel credit, autonomous shared Patch NPC and
  held watchtower sabotage implemented and engineering-verified;
  its checkpoint indexes actual results and fixture boundaries.
- B46: earned atomic fabrication, once-per-match private receipt, explicit Guild
  work/fabrication choice and independent recovery through the real Heart path.
  Its checkpoint indexes verification and ordinary/network fixture boundaries.
- B47: sampling/dash timing and HUD sizing corrected; core native/cooked checks,
  measured short graphics/transport profiles and ordinary 25-minute run pass.
  Full stress validation was deferred by user direction after a partial soak;
  fresh cold B47 checkout and actual human acceptance remain unrun.

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
The frontend now uses the saved regional map, with matching grid, fog and minimap
bounds. The migration map is retained for historical regression fixtures. B45
ordinary practice uses native travel, original timers, visits and eight paid jobs
to reach level five without kills or resource grants. Other authority/network
grants, positions and deadline advances remain labelled fixtures. B46 ordinary
practice earns Witness, four jobs and a Seal, then claims the protected Heart
using native travel and original timers without grants or clock advances. Its
network case earns two separate Seals through actual transactions with declared
travel/cooldown fixtures, then tests death drops and recovery without Witness.

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
tests do not establish full B47 acceptance. B42–B46 executed cases are indexed
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
| C23 | Passed | B44 four real alternatives; threshold/equipment guards; private once-only Witness; ordinary Survey hold; no gold/XP |
| C24 | Passed | Shared equipment/range/sight authorization covers Spark, Charter, theft and real watch control; authored cover/elevation checks pass in both cooked layouts |
| C25 | Passed | B44 rear/range/sight/protection/exposure and stationary channel guards; exact d20 enumeration and bounded math |
| C26 | Passed | Private Theft stream; valid completion draws once; replay/invalid/cancelled requests draw nothing; snapshot source/current target |
| C27 | Passed | Acceptance reserves 90-second stable wallet pair alongside 20-second skill cooldown; exact deadline and new-life retention |
| C28 | Passed | Atomic min(12,current gold); concurrent depletion, empty purses, actual contender gold and no XP |
| C29 | Passed | Failure retains funds; Alert 30 seconds and exposure four seconds; delayed private victim cue with no origin |
| C30 | Passed | Resident starts with 36; actual combat death/return keeps depleted wallet; NPC source uses common capability |
| C31 | Passed | Four-node main and five-node alternate; changed IDs/order/edges/bounds; real public/covered/outer route queries pass cooked; cook-safe navigation envelope |
| C32 | Passed | Native traversal earns three once-only 25 XP visits; placement/teleport baseline rejects; region-ID receipts retain through lives and clear on reset |
| C33 | Passed | Eight paid deliveries; seeded alternating destinations; original 3-second holds and 20-second acceptance delay; once-only rewards; paid budget separate from credits |
| C34 | Passed | Actual 12-damage/300 ms/1200 ms sentinel, leash/reset/60-second return; live greatest-damage credit and opaque ties; two-reward cap; dead finisher gets nothing |
| C35 | Passed | Ordinary native and cooked practice earn Witness, three visits and eight jobs to reach level five with 200 gold/eight credits and no kills or resource/build grants |
| C36 | Passed | Real equipped Analyze 14; 30-second offline/60-second global lockout/four-second exposure; delayed race, first-view invalidation and fixed restoration; defence flags retained |
| C37 | Passed | B46 atomic four-second fabrication; exact costs; interruption, creation failure, final revalidation, replay and retained once-only receipt; no normal seed |
| C38 | Passed | B46 ordinary starting Survey plus four real paid jobs, native travel and earned Seal win through the original ten-second protected Heart; no grants |
| C39 | Passed | B46 two remote contenders earn separate Seals; death drops, full carry rejection, recovery without Witness, contest and winning-item-only consumption |
| C40 | Passed | B47 actual level-five death at 1200 seconds loses 16 of 101 gold and waits 46; duplicate waiting death and withdrawal do not charge/reset/respawn; retained level-one boundary tests |
| C41 | Passed | Retained life/build/cooldown/knowledge and physical-drop checks plus B44 Witness, stable wallet, retry and Alert retention; old-life requests reject |
| C42 | Passed | B47 source/widget/effect audit plus 8-field observed and 21-field public allowlists; independent cooked clients expose no opponent canonical build/items; delayed conceal/old-revision/life checks pass |
| C43 | Passed | Authored resident navigates and autonomously uses actual learned/equipped/ranked Patch after a nonlethal hit; common interruption/cooldown path; real return retains build/wallet |
| C44 | Passed | B47 waiting withdrawal/roster audit and ten regional resets pass; match-scoped builds, budgets, RNG and craft/item state clear; authenticated resume is not retained |
| C45 | Partial | Current Editor/Game builds, 28 assets, Win64 cook and independent native/cooked remote clients pass; fresh cold B47 checkout not run during wrap-up; B46 cold checkout is historical; dedicated/remote delivery remain conditional |
| C46 | Passed | B47 calibrated cooked actual echo medians 91.43/93.77 and 153.24/149.71 ms; action acknowledgement p95 166.05/157.99 and 202.93/200.42 ms; delayed skill/book/theft/Seal/private-view semantics pass |
| C47 | Partial — deferred by user | Ordinary 1500.028-second run passes 37 checks with 26 audits; separate 90-minute harness stopped at user direction after 600.1-second audit and five forced lethal batches; final stress percentiles unrun |
| C48 | Passed (reporting) | B47 ledger/report distinguish retained/new behavior, actual passes, failed intermediate runs, overrides and user-deferred stress; fresh cold B47 checkout and human validation remain explicitly unrun |

The ordinary 25-minute automated session passed. The separate 90-minute technical
stress run was stopped at user direction after about ten measured minutes and is
deferred. No human matches or build-diversity/long-session pacing study occurred.
Fresh cold B47 checkout, dedicated packaging and remote Git/LFS distribution remain
unrun or conditional. Carry these gaps into the next version; no further feature
scope is inferred from closing the v0.4 checkpoint.
