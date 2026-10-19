# v0.4 baseline review and implementation readiness

Historical preparation review. [B40 baseline verification](B40_BASELINE_AUDIT.md)
has since passed; track new implementation in the
[implementation record](IMPLEMENTATION_REPORT.md) and working ledger.

Reviewed 19 October 2026 against Unreal source checkpoint
`4cc9cbde3e696eaef2f0defff1f0ddec48e5ba89`, the supplied master revision 4 and v0.4
specification revision 1. This is preparation for B40, not B40 completion.
[FeatureLedger.csv](FeatureLedger.csv) records the inspected baseline separately
from unimplemented v0.4 work. The specification and JSON remain proposed tuning.

## Design review

The release scope follows the revised master plan: classes replace heroes; six
allocated attributes and learned/ranked skills persist; combat and utility share
four slots; books support cross-class learning; and real peaceful work earns a
Seal. The smaller five-level, 25-minute regional profile is explicitly separate
from the full 60–90 minute vision. No scope conflict was found in those contracts.

The JSON companion matches the specified class vectors, nine skills, six
subclasses, level thresholds, book catalog, theft arithmetic, work rewards,
fabrication costs and timers. It is source tuning, not a loaded Unreal registry.
Keep prose and JSON synchronized when implementation resolves tuning or layout.

Companion schema version: `1`. SHA-256 over UTF-8 text normalized to LF:
`6b0e3a6ac94830536e29afc428f0b865dbba7db1b7e1167e4823db4ce6954e5c`.

Each class has a starting Witness path at threshold 12: Laborer/Heave STR 12,
Rogue/Survey WIS 12, Scholar/Analyze INT 12. Four deliveries yield the four credits
and 100 gold needed for a Seal before other spending. Eight deliveries plus three
visits yield 435 XP against the 420 cap. The specified theft example succeeds on
11 of 20 rolls. These are specification checks, not gameplay demonstrations.

## Existing implementation and evidence

| Area | Inspected baseline | v0.4 treatment |
| --- | --- | --- |
| Engine and modules | `Config/PlatformLock.json` pins 5.8.3 CL 58210709; runtime `Lockdown` and editor bootstrap `LockdownEditor` | Retain; no engine or mandatory GAS migration |
| Movement | `LDCharacterMovementComponent` uses saved sprint moves and authority root-motion dash | Retain prediction/collision; derive speed and Step distance through this path |
| Persistent state | `LDGameMode` owns contender records; `LDViewTypes` separates private owner state from observed records | Extend with private build, stable wallet, reward and item state |
| Views | Owner-only relay and pointer-free observations; current/explored mask, private wards and public sources | Retain privacy; adapt spatial data and audit new skill cues |
| Combat and life | Combat director, simultaneous damage, old fixed melee/assassination/dash, death/return receipts | Retain ordering/counterplay; replace profile values and activation permissions |
| Objectives | Real courier holds, Seal pickup/drop and Heart completion; acquisition/defence fixtures | Extend receipts and item lifecycle; implement earned fabrication |
| UI and assets | Native UMG, Enhanced Input, bootstrap and 23 generated packages in the asset manifest | Add real class/development/inventory/region assets; preserve saved-asset workflow |
| Delivery | Existing build, native probe, network, reset, package and clean-clone scripts | Reuse; update changed expectations without discarding foundation regressions |

The [B36 report](../v03/implementation/TestReport.md) records cold builds, 23-asset
validation, eight native rule/schema cases, packaged private-view/combat/objective
checks with two remote clients, ten resets and a ten-minute measured soak.
Its authority was a headless listen process with a diagnostic local connection,
not a dedicated Server target. The reported rendered-client acknowledgement p95
was 249.60 ms, close to the 250 ms target; remeasure after new scope.

Fresh `Scripts/Preflight.ps1` passed during this review. Evidence is repository-root
`Artifacts/20261019-090955-044-Preflight/Environment.json`. Engine discovery, the
two supported compiler versions, Windows SDK and .NET Framework SDK were checked.
No cold build, asset commandlet, gameplay/network probe, package or human match
was rerun for this documentation change. Prior evidence remains historical.

Documentation validation passed: 63 Markdown files have no broken local links;
all four frozen v0.3 snapshots match the originals byte for byte; the working
ledger has 39 unique feature rows covering C01–C48. Catalog references, class
budgets, starting Witness paths, level thresholds, reward arithmetic, the theft
and death examples, and region connectivity were checked. Local results are in
`Artifacts/V04DocumentationValidation.json`; these checks do not execute Unreal.

## Migration risks to address in B40–B46

| Source | Finding and required change |
| --- | --- |
| `Source/Lockdown/Public/Vision/LDViewTypes.h`, `Public/Core/LDPrototypeTuning.h`, combat and UI consumers | Owner HP and tuning damage are integers. Derived melee can be 21.5. Select one consistent fractional representation across damage, HP, replication, UI and tests. |
| `Public/Vision/LDViewTypes.h`, view serialization, fog/minimap consumers, `Config/BootstrapLayout.json` | Vision is fixed at 60×40 cells of 80 cm over 4800×3200 cm. The proposed towns and WildCrossing lie outside those bounds. Make geometry, view encoding/limits and client mapping agree with region data; test another topology. |
| `Private/Framework/LDPlayerController.cpp`, combat director | Assassinate and Dash bind directly to combat actions. Replace all ordinary shortcuts and RPC/context entry points with equipped-slot and build-revision validation. |
| `Public/Core/LDRules.h`, tuning, objective director | Old thresholds are 30/80 XP, courier pays 15 XP, the cap is 720 seconds and death uses the old formula. Replace the whole active profile consistently; do not layer legacy bonuses onto derived stats. |
| `Public/Objectives/LDObjectiveRules.h`, objective director and UI | Guild/West/Heart positions and three district notices are hard-coded; courier is repeatable with one destination. Introduce region IDs, alternating towns, eight-payment budgets, first visits and named-region notices. |
| Objective director, `ALDSeal`, owner state | A local fixture supplies a Seal; ownership supports the migration demonstration. v0.4 permits multiple independently earned Seals, one per crafter/carry slot, plus physical unread books. Audit all global item assumptions and retain unique IDs through death/drop/cleanup. |
| `Private/Framework/LDGameMode.cpp` | Login creates a fresh record; logout removes it; PreLogin only rejects finished matches. Add Ready/start and roster lock. No authenticated resume exists to preserve; new guests wait for the next lobby after start. |
| Runtime module dependencies, combat/objective entry points | Focused native C++ with no GAS dependency exists. Extend one activation/arbitration path for player and trained NPC; do not add a parallel ability/damage system. |

Catalog validation must cover IDs, references, costs, bounded formulas and cues.
Record the imported schema version/checksum when generating actual assets.
Initialize ordinary resident attributes explicitly when adding common wallet and
skill capabilities; do not infer private target WIS from a public model.

## Remaining acceptance and next work

The old B37 human comparison remains pending; advancing scope does not convert it
into a pass. Dedicated Server packaging remains conditional on a compatible source
engine. Remote repository/LFS delivery, authenticated resume, full conquest,
population/economy parity and long-session human pacing remain gaps.

Complete fresh B40 prerequisite evidence before implementing B41. Then follow
[B41–B47](07_BUILD_PLAN.md) and record [C01–C48](08_ACCEPTANCE_AND_PLAYTEST.md)
against actual builds. Legacy fixed-kit/death/timer tests need explicit new-profile
expectations. Keep normal progression, labelled test fixtures and technical-soak
overrides distinguishable. No v0.4 acceptance result has been earned by this review.
