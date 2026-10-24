# v0.7 transition handoff

24 October 2026. Start implementation with **B70 audit and reconcile** after the
[B67 development closeout](v06/B67_CLOSEOUT.md). Frozen v0.6 runtime: **`920c4ae`**.
The B67 commit adds documentation/evidence only. User direction: minimal checks,
no lengthy tests, defer human review, move to v0.7. Permission to commit persists;
no push requested. Do not redo B61–B66 or restart long acceptance automatically.

## Read order and authority

1. [Current continuation](CONTINUATION.md), [B67 acceptance matrix](v06/B67_ACCEPTANCE.md)
   and [B66 evidence](v06/evidence/B66/Summary.json) establish delivered scope.
2. [v0.7 decisions/migration](v07/00_DECISIONS_AND_MIGRATION.md),
   [scope/profile](v07/01_SCOPE_AND_RELEASE_PROFILE.md) and
   [build plan B70–B77](v07/12_BUILD_PLAN.md) govern the new profile.
3. [Combat](v07/02_COMBAT_RUNTIME_AND_EFFECTS.md),
   [targeting/vision](v07/03_TARGETING_VISION_AND_TERRAIN.md),
   [build/gear](v07/04_PROGRESSION_EQUIPMENT_AND_LOADOUTS.md),
   [route rules](v07/09_VICTORY_AND_MATCH_STATE.md) and
   [authority/UI](v07/11_AUTHORITY_NETWORKING_AND_UI.md) define semantics.
4. Read catalogs/data and [import contract](v07/16_DATA_CONTRACT_AND_IMPORT.md)
   before building native assets; [build prompt](v07/14_CODEX_BUILD_PROMPT.md)
   and [acceptance](v07/13_ACCEPTANCE_AND_PLAYTEST.md) define delivery.
5. [Master revision7](master/README.md) supplies long-term intent. Frozen
   `docs/v07/reference-v06` is historical specification, not a snapshot of the
   actual implemented source or proof that its defaults match every adapter.

The master/v0.7 specification already exists as user-authored local changes:
13 modified tracked master files, two new master documents, and untracked
`docs/v07/`. B67 does not change/stage them. Preserve those bytes; the
[supplied-spec manifest](v06/evidence/B67/SuppliedSpecSnapshot.json) captures
current provenance. Record/adopt their version in B70 before runtime mutation.
Their checker passed 931 specification assertions; no v0.7 engine feature follows
from that result. The runtime still loads staged Content catalogs, never docs.

## Delivered defaults versus selected v0.7 changes

| Area | Actual v0.6 baseline | v0.7 contract / migration concern |
| --- | --- | --- |
| Match selection | Historical `Profile=V05` and normal development maps use economy runtime;1500s, level5,1–4 locked contenders | Separate versioned `v06_regression` and `v07_integration` selection;2100s/level15 only in new profile. No live switching or cross-profile grants |
| Class stats | Laborer/Rogue/Scholar each have 60 natural points,8–16 range, two known/equipped starting skills | New templates use 53 points. Audit/normalize legacy starts for v0.7 only, preserve primary/secondary direction and validate old checks/Witness. Keep v0.6 unchanged |
| XP | Cumulative0/60/150/270/420, capped420;300XP is level4 | New0/75/150/300/450/... table, level15. Do not mutate shared constants before profile separation |
| Skill/quick slots | Four shared skills, four quick references; nine nonempty legacy skill IDs | Five skills/three quick references, new D binding after conflict detection, old quick4 unbound with feedback. No free economic utility bar |
| Wire/data shape | `FLDBuildPlan` explicitly serializes four slots and four-bit skill IDs | Combined legacy+28-new catalog exceeds this encoding. Version/validate request/owner wire formats and bounds; widening only the enum is insufficient |
| Resources/effects | Existing HP/ability/channel/director path; no MP system found in current source scan | Add MP and tactical effects through audited native action boundaries, no automatic GAS replacement |
| Items |24 unique carried instances; cargo mass, quick-use policy, bank and shared reservations | Six gear bindings plus three references; avoid duplicate ownership/mass/cooldowns, separate natural/effective stats, revise death retention per item policy |
| Charter | Witness+distinct endorsements+100g split, bound registration,4s activation,30/60 public window, respawn+30 recovery | Retain economic/public-access contract; opening never grants exclusive purchase-owner victory |
| Claim | Ten-second physical channel; damage/movement/proximity contest in v0.6 | New control/displacement/explicit-interrupt rules; damage/proximity alone no longer cancel in v0.7. Preserve old regression behavior via profile gate |
| Other routes | Normal v0.6 disables old physical-Seal/credit bypass; legacy diagnostics are isolated | Implement real global siege and personal keys/Heartbreaker leases; never revive the old bypass as an adapter |
| Institutions | Eight funded ticks through 1440s; East shortage600–780; locked-N quotas | Retain bounded totals in35-minute profile, no automatic extra ticks/mints/resets |
| Acceptance | Local engine/earned/UI evidence; extended checks deferred | Carry V06-D01–D08 explicitly. All 28 abilities/40 items/12 recipes/four routes remain staged v0.7 scope; B71–B73 alone is partial |

## First implementation work: B70

- Record the actual source baseline, supplied-spec checksums, pinned engine,
  runtime launch selector and the48-row inherited coverage/deferrals. Create a
  working v0.7 ledger from its template; 55 planned checks begin Not run.
- Audit actual formulas, character presets, skill costs/cooldowns, Witness,
  bank/death/reservation/claim ordering and reflection/wire bounds. The table
  above is an entry point, not the completed B70 audit.
- Decide profile, catalog/import and serialization adapters before changing
  combat/progression constants. Record explicit differences in prose/data/assets
  together; protect the frozen v0.6 regression profile at clean match boundaries.
- Proceed B71 combat/MP, B72 geometry/control, B73 build/gear/preparation, B74
  acquisition/assembly, B75 full tactical catalog, B76 world/four routes, B77
  integrated evidence. Keep each milestone runnable and commit focused changes.
- Use only the affected brief checks for each real change. Human review remains
  deferred. Keep longer technical runs in the named backlog until scheduled.

## Code entry points and invariants

| Responsibility | Start here |
| --- | --- |
| Match/profile/owner state and ordering | `Source/Lockdown/Private/Framework/LDGameMode.cpp`; `Public/Framework/LDGameMode.h` |
| Class/XP/formulas/request serialization | `Source/Lockdown/Private/Development/LDCharacterBuild.cpp`; `Public/Development/LDCharacterBuild.h` |
| Existing action execution and damage sampling | `Source/Lockdown/Private/Combat/LDCombatDirector.cpp`; `Public/Combat/LDCombatDirector.h`; `Private/Framework/LDPlayerInput.cpp` |
| Economy catalog/receipts/atomic ownership | `Source/Lockdown/Private/Economy`; `Public/Economy`; `Content/Lockdown/Data/economy_rules.json` |
| Shared bag/peer reservations/capacity | `Source/Lockdown/Private/Inventory`; `Public/Inventory/LDInventoryTypes.h`; `Private/Inventory/LDInventoryTypes.cpp` |
| Production/Charter/Heart | `Private/Economy/LDProductionDirector.cpp`; `Private/Objectives`; `Private/Framework/LDPlayerProduction.cpp` |
| Owner-only service presentation/input preferences | `Private/UI/LDInventory*`; `LDHUDModel.cpp`; `LDControlSettings.cpp`; `Private/Framework/LDPlayer*.cpp` |
| Filtered observation | `Source/Lockdown/Private/Vision`; rule allowlists in `Private/Tests` |
| Bootstrap and brief checks | `Scripts/BootstrapAssets.ps1`; `BuildEditor.ps1`; `RunCharterCheck.ps1`; `RunEconomyUICheck.ps1`; `CheckEconomyEvidence.py` |

Preserve CharacterMovement, Enhanced Input, UMG/Slate, unique instance/life/body/
request identities, owner-only private state and the shared lock/receipt authority.
The frozen baseline’s observed tuple has8 fields and GameState allowlist34;
future schema extensions need explicit privacy policy and affected checks.
Every UPROPERTY field needs its own declaration; comma declarations previously
left later fields unreflected. Shared serializers have hardcoded shape limits.
No client-computed price/fee/reward or distant live market discovery. Failed
allocation/reservation/output must consume nothing. Due combat/death precede
noncombat commitments and terminal outcomes stop later economy/window work.

## Toolchain, launch and evidence

Pinned **UE5.8.4 CL58902052**, installed engine, Win64 Development; last successful
build used MSVC14.44.35229 and Windows SDK10.0.26100.0. Keep worker cap4 and run
Unreal operations sequentially with Source/Config frozen. Build commands and pin
live in `Scripts/Common.ps1`, `Scripts/BuildEditor.ps1` and `Config/PlatformLock.json`.

Current source game: `./Scripts/OpenStandalone.ps1` uses the normal frontend;
`-NavigationCheckpoint` directly opens the historical economy map/profile.
Those commands open a visible interactive game when the user wants to play;
they were not launched for B67. Future focused engine checks are available, not
an automatic instruction to launch them now. The old packaged build is stale
for v0.6. Do not label it a current v0.6/v0.7 executable.

B66 retains build +23 rules/56 authority/39 controlled earned-route checks and
84 native UI checks per layout. B67 only rechecked source/report/capture hashes,
13 retained ledger files and both document models. See [B67 audit](v06/evidence/B67/Audit.json)
and [deferred backlog](v06/B67_CLOSEOUT.md#deferred-verification-backlog). Human,
OS-scale, independent-client latency/privacy, fresh packaging, normal timed
sessions and restart/performance acceptance are not supplied by these results.
