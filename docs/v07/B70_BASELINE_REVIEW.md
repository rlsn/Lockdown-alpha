# B70 — source audit and adopted v0.7 adapters

24 October 2026. **B70 complete: source/build audit and migration decisions.**
The runnable game remains the frozen v0.6 economy. B71 starts combat/MP;
no v0.7 gameplay or release acceptance follows from this audit.

## Baseline and evidence

Repository HEAD at audit: `289501c` (B67 documentation closeout). Actual runtime:
`920c4aede4fab1338bbec4fc728ebcee14f56407` (B66). Source/Config/Content/project
have no changes relative to B66. [Runtime inputs](evidence/B70/RuntimeInputs.json)
inventory the physical checked-out files, including saved native assets;
[adopted specification](evidence/B70/AdoptedSpec.json) pins the supplied master
revision 7 and v0.7 revision 1 bytes against B67's supplied-spec snapshot.
Those original specification files are adopted in this commit without editing
their contents. Their statements about no source inspection describe their
original authoring task; this report supplies the subsequent source audit.

| Fresh B70 check | Result | Evidence |
| --- | --- | --- |
| Win64 Development Editor build, cap 4 | Passed; 7 actions, 60.94 s; MSVC 14.44.35229, SDK 10.0.26100.0 | [Build log](evidence/B70/BuildEditor.log) |
| Existing generated assets, Validate only | Passed; 28 packages, 0 errors; 4 existing CrowdManager/nav warnings | [Bootstrap log](evidence/B70/Bootstrap.log), [summary](evidence/B70/Verification.json) |
| Specification model | 931 assertions passed; document/data only | [Model output](evidence/B70/SpecModel.txt) |
| Frozen inputs, ledgers and adapter arithmetic | Passed | [Audit](evidence/B70/Audit.json), [checker](../../Scripts/CheckV07Baseline.py) |

Engine `Build.version` and [PlatformLock](../../Config/PlatformLock.json) agree:
UE **5.8.4 CL58902052**, installed engine. Preserve the lock's historical
`baselineRevision`; it is engine-update provenance, not this milestone's source
revision. [Engine version](evidence/B70/EngineVersion.json) records the observed
installation. The successful local rebuild is not a cold checkout/cook/package.
Build emits a Visual Studio SDK integration warning; compilation/link succeed.

Only short affected checks were run. B66's 23 rules/56 authority/39 controlled
earned-route and 84 native UI checks per layout remain **retained**, not rerun.
Long sessions, network/privacy, packaging and human review stay explicitly open.

## Actual source inventory

Paths below are relative to `Source/Lockdown/` unless stated otherwise. These
are inspected implementations, not deductions from frozen reference documents.

| Domain / source | Delivered behavior and B70 reconciliation |
| --- | --- |
| `Private/Framework/LDGameMode.cpp`, `Public/Framework/LDGameMode.h` | `InitGame` selects `Profile=V05` or unqualified development maps for native UI/economy. `V04` retains historical development without economy. Regional map names also force development for unknown profile strings: an invented V07 URL does **not** select v0.7 safely. Lobby start locks 1–4 humans; no autonomous contenders or reconnect restoration. |
| Same file: `StartDevelopmentMatch`, `SyncOwner`, `EndContenderLife`, `ReturnDueContenders` | Economy initializes once at start; persistent contender Session GUID, match generation, life counter and revision protect owner commands. Owner state copied only to its controller; deaths retain build/XP/cooldowns/bank/Witness/endorsements/registration. New body increments life. Logout retires/removes contender; B70 does not redesign reconnect. |
| `Private/Development/LDCharacterBuild.cpp` / public header | Nine nonempty skills plus None, three legacy classes; four skill slots; natural 8–16; level 1–5, XP capped 420. Level thresholds 0/60/150/270/420; +2 attribute/+1 skill point per level. Subclass at level 4, permanent/class-linked. Class learning differs from universal book learning. |
| Same file: `MaxHP`, `BasicDamage`, `SpeedFactor`, `SkillValue` | HP clamp(100+5*(CON−10),75,150); basic damage clamp(20+0.75*(STR−10),14,28); walk/sprint 180/300 times clamp(1+0.01*(DEX−10),.90,1.10). No MP/armor/barrier/status system in inspected runtime. Legacy magnitudes and scores are tabulated below. |
| `Private/Combat/LDCombatDirector.cpp`, `LDSkillExecution.cpp` | Native server sample every .05 s, ordered intents, body identity, build snapshot, target validation and batch damage/deaths. Instant legacy actions and held skill-task Validate/Accept/Commit adapters; costs are cooldowns, not mana. Damage cancels held tasks/preparation/production and Heart. Channels complete after damage, then deaths and noncombat services; objective/deadline processing is later. Late ticks sample current pose once; no chronological event drain. |
| `Private/Development/LDDevelopmentDirector.cpp` | 120 cm station, six combat-quiet seconds, stationary 2 s preparation, 3 s book read; stale/released/changed holds reject. Four sold legacy books Step/Spark/Patch/Parley at 40g teach rank 1 without class restriction. Duplicate read retains book. Learned/equipped/rank/cooldown remain distinct. |
| `Private/Development/LDWorldChecks.cpp`, public `LDWorldChecks.h` | Witness: one retained GUID, no gold/XP; equipped Heave/Analyze/Parley/Survey score ≥12, 120 cm/sight, ordinary held skill path. Theft rolls 1–20 privately; DEX/WIS score, rank/subclass, 90 s persistent pair retry; max 12g transfer, no mint. NPC purses survive replacement bodies; official institutions have no lootable combat body/purse. |
| `Private/Economy/LDProductionDirector.cpp`, `LDEconomyProduction.cpp` | Survey extraction score12, 1 s hold; Analyze coupon score14, 2 s hold; 30 s coupon saves2g only on original Toolkit/Core recipes. Basic gathering/crafting need no skill/Witness. Exact input locks, projected net capacity and staged physical output precede one ledger commit; failure consumes none. |
| `Private/Inventory/LDInventoryTypes.cpp`, `LDInventoryDirector.cpp` | 24 unique instance records, four quick references, no gear bindings. Weight Book/Ward/Fiber1, Seal/Crystal2, Ore3, Toolkit4, Core6. Capacity60+2*(STR−10); final outgoing/incoming projection; references/reservations add no mass. Books and wards currently quick-usable, commodity goods are not. Bind/unbind currently immediate; B73 replaces it only in v0.7. |
| `Private/Inventory/LDTradeDirector.cpp`, `Private/Economy/LDBankDirector.cpp`, `LDEconomyLedger.cpp` | One item reservation authority across peer/production; revision/digest Ready + separate Confirm. Offered gold stays stealable. Physical Treasury service: 120 cm/sight, six seconds quiet; deposit fee ceil(2%); carried wallet/bank split for Charter. Checked integer balances/revisions, receipt replay and staged transactions. No client price authority. |
| `EndContenderLife`, inventory `EndLife`, development `DropBooks` | Production and trade cancel/release before dropping. Cargo/books drop; **unused ward charges are consumed**, not dropped. Wallet loses ceil(15%); bank safe. Legacy return delay min(60,10+4*(clamp(level,1,5)−1)+5*floor(elapsed/300)); four seconds protection/full HP. No free return items. v0.7 explicitly replaces ward loss with consumable drop, delay/protection and gear retention in B73. |
| `Private/Economy/LDEconomyCharter.cpp`, `LDEconomyInstitutions.cpp` | Witness + distinct Supply/Industrial +100g wallet/bank burn; registration retained, 4 s civic activation, 30 s global warning/60 s public opening. Before-use death waits until actual respawn+30; activated opening survives buyer death. Eight institutional ticks stop at1440; shortage600–780; budgets fixed from locked N. |
| `Private/Objectives/LDObjectiveDirector.cpp` | Normal economy rejects old Seal fabrication/private access. Ten-second physical claim; ordinary damage/movement cancel and enemy within240cm resets progress. Charter scheduled-close checks exist, but concurrent claims/start ordinal/OR personal access and late-event draining are new work. Siege and keys/artifact are absent in normal economy. |
| `Private/Vision/LDViewTypes.cpp`, `Private/Tests/LDViewTests.cpp` | Eight observed fields, no canonical actor reference/private build; GameState allowlist34 fields. Wire validates kind/presentation/cue/grid/count; byte entity count and bounded sensor counts need explicit extension for towers/emitters/cues. New native actor replication must not bypass filtered observation. |
| `Private/UI/LDControlSettings.cpp`, `Private/Framework/LDPlayerInput.cpp` | Saved controls schema1 validates exact default action count; Numbered parser stops at4. Four skill/four item handlers, HUD/build and quick references all hardcoded. Simply adding D or growing an array breaks preferences/dispatch; B73 must migrate accepted settings field by field with conflicts visible. |

Starting owner state is **0 carried gold, 0 bank, 0 XP and one ward charge**
(`Public/Vision/LDViewTypes.h`); Inventory.Initialize turns that charge into one
instance once using bInitialized, not on respawn or kit selection. StartPlay
creates eight residents; one has a real private Patch build, not an autonomous
strategic contender. Market endowment is450g. Nonplayer purse registration is
lazy: WorldChecks.WalletId creates a36g account and ResidentEndowment records it
once; returned bodies reuse the existing wallet ID. That helper also serves
nonresident NPC bodies, including the sentinel. Do not assert a constant738g
initial total from the eight-resident example: use the actual registered accounts
and endowment receipts. B71/B74 must preserve this accounting boundary rather
than count an endowment as a combat reward or grant fresh purses on return.

Budget audit: eight25g courier rewards, two25g sentinel rewards, three80g Core
orders and one50g Supply order per contender give540*N; one shared40g shortage
order gives the retained540*N+40 reward cap. Markets create84 initial commodity
instances (the separate player ward is not market stock). Eight producer
opportunities per region are funded burns, not reward mints. New v07 camps/
towers/boss add at most642g; adopted reward bound becomes540*N+682 without
changing endowments or adding institutional ticks after1440s.

## Legacy class and skill adapters

Attribute order is STR/DEX/CON/INT/WIS/CHA. All existing classes have **60**
points, not53. Preserve their names, existing two rank-one starters, legacy
learning access and earned progression. New six teaching presets are additional
classes, never aliases for these three. The chosen v0.7-only starts are in
[B70_ADAPTERS](B70_ADAPTERS.json); no source/catalog is mutated at B70.

| Class | Actual v0.6 | Adopted v0.7 | Known/equipped starters | Witness through retained starter at rank1 |
| --- | --- | --- | --- | --- |
| Laborer | 12/9/12/8/10/9 | 11/8/10/8/8/8 | Heave, Patch | STR11→12; level2/75XP, one point |
| Rogue | 9/12/9/10/12/8 | 8/11/8/8/10/8 | Pickpocket, Survey | WIS10→12; level3/150XP, two points; or rank2 Survey at level3 |
| Scholar | 8/9/10/12/9/12 | 8/8/8/11/8/10 | Spark, Analyze | INT11→12; level2/75XP, one point |

Tie resolution preserves STR/CON, DEX/WIS, INT/CHA direction. Each has 8 each
+3 primary/+2 secondary. Starting capacity becomes62/56/56 (previous64/58/56).
These are adopted implementation choices, not evidence of playable normalized
journeys. B73 must exercise old score thresholds/Witness on native paths;
V06-D08 remains open until normal class journeys are actually supplied.

| Skill | Native magnitude / check (R=trained rank, A=natural attributes in v0.6) | CD seconds | Action/cost boundary |
| --- | --- | --- | --- |
| Step | clamp(360+10*(DEX−10)+20*(R−1),300,420) cm | 7 | accepted collision-valid dash, .2 s |
| Ambush | clamp(60+.5*(STR−10)+.5*(DEX−10)+4*(R−1),50,80) | 18 | rear/unalert/quiet, .5 s windup |
| Spark | clamp(24+1.25*(INT−10)+4*(R−1)+Channeler3,18,45) | 6 | observed target, .4 s windup |
| Patch | clamp(25+(WIS−10)+4*(R−1)+Medic3,18,40) | 12 | self heal, 1 s channel |
| Pickpocket | d20+floor((DEX−10)/2)+R+Cutpurse1 vs12+floor((targetWIS−10)/2)+alert2 | 20 | 1 s channel, separate 90 s pair retry |
| Parley | CHA+2*(R−1)+Envoy2 | 8 | 2 s channel |
| Analyze | INT+2*(R−1) | 6 | 2 s channel; Witness12/coupon14 |
| Survey | WIS+2*(R−1)+Watcher2 | 6 | 1 s channel; Witness/extract12 |
| Heave | STR+2*(R−1)+Hauler2 | 6 | 2 s channel; Witness12 |

All nine currently cost **0 HP/0 MP/0 gold** to cast. Cooldown starts on accepted
action/channel, survives interruption/death and is copied across preparation.
Theft retry is separately consumed on Accept, with RNG drawn only at Commit.
New MP/release costs must not be retroactively imposed on economic skills.
Subclasses are retained legacy modifiers; new presets have no implicit legacy
subclass. v0.7 mastery level gates/costs replace the legacy early rank rules.

Normalization preserves thresholds rather than silently lowering them. At rank1,
Survey12 requires WIS12, Analyze14 requires INT14, Parley12 CHA12 and Heave12
STR12. Every normalized class can invest toward these by level15. Scholar can
reach coupon14 at level4 with three INT points, or level3 with one INT point
and rank2; Rogue's rank2 Survey reaches12 at level3. This is arithmetic, not
normal-route evidence. New templates without a Witness starter may buy Parley
for40g; the existing 15g-surplus route then needs another25g job, leaving0g.

## Adopted implementation decisions and dependency order

1. **Profile boundary first in B71.** Keep V05 as the compatibility alias for
   `v06_regression`; add explicit `v06_regression` and `v07_integration` selectors
   resolved once in InitGame. Reject unknown selectors rather than region-name
   fallback. Match/generation/profile/catalog version travels on requests and
   owner snapshots. Match start resets the full match domain; no live profile
   switch, save conversion or cross-profile grants. Until this is implemented,
   use only existing V05 URLs; v07 profile names below are reserved designs.
2. **Keep the native architecture.** Extend LDCombatDirector/LDSkillExecution
   with one dispatcher and typed packet/effect records; retain CharacterMovement,
   navigation, Enhanced Input, UMG/Slate and server-only inventory/economy. Keep
   contender-owned cooldown/MP/knowledge and life-owned statuses distinct.
   Legacy economic tasks use their Validate/Accept/Commit callbacks, score/CD/
   hold costs unchanged, through the common action legality path. Legacy Step/
   Ambush/Spark/Patch keep audited numeric rank formulas initially (Ambush
   physical, Spark magical, Patch healing); apply new status/target/release
   legality in the v07 branch. Do not silently scale their numbers to new HP
   or add an extra INT multiplier. Measure their balance before an explicit
   synchronized tuning revision. Basic attacks and new skills use v07 formulas.
3. **Version wire before expanding content (B71 foundation, B73 callers/UI).**
   Freeze legacy numeric skill IDs0–9; append28 stable IDs10–37 per the adapter
   contract. New build wire has explicit schema7/profile/catalog digest, bounded
   16-bit skill IDs, exactly38 rank entries and5 slots; reject malformed shape,
   unknown IDs, wrong profile/digest before allocation or spending. Keep the
   legacy four-slot serializer path for regression. Update owner RPC/state and
   book/trade/request bounds together; growing the enum alone is insufficient.
   Test roundtrip/rejection/old-life/idempotency natively in the affected slice.
4. **Import into staged Content, never runtime docs.** Add schema7 catalogs under
   `Content/Lockdown/Data/V07/`; explicit staging/hash manifest and separate
   generated `/Game/Lockdown/Generated/V07/` assets via Bootstrap. Keep existing
   schema1 CharacterCatalog/economy asset/data unchanged for regression. The old
   economy parser hard-requires1500s: adapt its profile validation separately
   from bounded economic schedules/budgets. Import stable definitions/book
   wrappers and operations with strict schema/reference/units checks. Reject
   an unimplemented operation at activation/import of the runnable slice; store
   planned content separately until real effects exist. B70 does not copy all
   data into Content and advertise it as working. Preserve Ore/Core/Fiber case.
5. **Chronological scheduling is real work (B71/B72 foundation, B76 outcomes).**
   Use integer ms for new action/effect events; adapt existing seconds endpoints
   once at a named boundary. Drain scheduled events in time order; movement →
   due combat/expiry → simultaneous deaths/release/drop → institutions/noncombat
   → claims → deadline. Keep v06 behavior isolated. Current late pose sampling
   cannot certify inclusive endpoints, concurrent claims or stalled ticks.
6. **Build/item migration in B73, acquisition in B74.** Normalize only new-match
   v07 starts; natural gates separate from effective stats. Five shared skills,
   three item references, six binding references to the same24 instances. Keep
   common locks/ledger/lineage; add definition/quality/cooldown/family/binding
   fields, not a second inventory. Legacy wards join v07 consumable drop policy.
   Books become quiet inventory reads rather than combat quick items. Settings
   migration preserves compatible actions1–3, removes Item4 with feedback and
   assigns D only if free; otherwise Skill5 stays unbound with a visible prompt.
7. **Complete content/world/outcomes in B75/B76, integrated evidence B77.**
   Add real operations/cues/sources, never inert definitions. Siege is new global
   tower destruction, keys/artifact are personal leases, retained Charter public.
   All share one ten-second claim; new damage/proximity semantics do not alter
   the regression profile. B71–B73 is a partial tactical review build.

These decisions adopt the supplied numbers without changing the release contract.
Later deviations must update prose, adapter contract, Content/assets and affected
evidence together. [Working ledger](FeatureLedger.csv): C001 source/build audit
complete; C002–C055 **Not run**. Templates and acceptance_index remain untouched.

## Inherited acceptance and open verification

[InheritedAcceptance.csv](evidence/B70/InheritedAcceptance.csv) carries all48
E01–E48 rows with exact B67 classifications/deferral IDs, retained evidence and
affected v07 checks. Categories remain1 document audit,27 local engineering,
17 partial,3 wholly deferred; they are not v07 passes. The original
[B67 reconciliation](../v06/B67_ACCEPTANCE.md) remains frozen.

| Inherited gap | Follow-up in v0.7 |
| --- | --- |
| V06-D01 independent clients/privacy/latency | Affected B71–B76 authority paths; C047/C048/C050 at B77 |
| V06-D02 cold checkout/current package | C049, B77; this rebuild/asset validation does not close it |
| V06-D03 normal1/4-player economy sessions | Separate retained gap; new C054/C055 routes and35-minute runs at B77 |
| V06-D04 restarts/performance/leaks | C051, B77 when scheduled |
| V06-D05 human understanding/balance | C052/C053/C055; human review remains deferred |
| V06-D06 competing transactions/network endpoints | Focused affected checks during B71–B76; C025/C041/C044/C047/C048 |
| V06-D07 physical input/OS scaling/modal completeness | C003/C049/C050; native fixture captures remain partial |
| V06-D08 normal class Witness journeys | B73 normalized-score native checks, C035/C047/C054 normal evidence |

## Reproduction and next work

From repository root in PowerShell7:

```powershell
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/BootstrapAssets.ps1 -Mode Validate
python docs/v07/tools/check_spec.py
python Scripts/CheckV07Baseline.py --output Artifacts/B70-Recheck.json
```

The last command intentionally checks the B70 frozen bytes; later migration
changes should be explained by later milestones rather than rewriting history.
Current playable source: `./Scripts/OpenStandalone.ps1`; direct economy map:
`./Scripts/OpenStandalone.ps1 -NavigationCheckpoint` (V05). No fresh package.

**Next: B71.** Implement profile/wire/import isolation first, then MP/common
action release, typed damage/barriers and core statuses with Arc Bolt, Anchor
Bash, Purge Touch and Null Bell. Run affected short authority/resource checks;
carry longer B77 checks as named gaps. Do not reopen B61–B67 acceptance work
merely to start combat.
