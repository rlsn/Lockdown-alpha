# B80 — v0.8 baseline audit and isolated import

25 October 2026. **B80 is complete for source, provenance, import and ledger
work. B81 slots/traits is next.** The build IDs are B80 Audit/import, B81
Slots/traits, B82 River core, B83 Fields, B84 Catalog/acquisition, B85 World
integration and B86 Native usability. This adopts the seven stages in the
[implementation plan](11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md); it does
not certify a complete v0.8 runtime.

## Actual source and retained progress

Audit HEAD: `7d9d66dfe77407f2105f2552b297709a0366146a` (B77 closeout).
Runtime/source/assets: `ddf7be5aaf74cbf8be013308e130233bcb1a410c` (B76).
B77 changed documents/evidence only. The separate web checkout is not the
implementation target.

The native v07 integration already implements the 28 retained tactical
abilities, 40 items, 12 recipes, paid acquisition, preparation/mastery, finite
encounters, leases and all four Heart routes. See
[B76](../v07/B76_IMPLEMENTATION.md) and
[B77 acceptance](../v07/B77_ACCEPTANCE.md) for the scope of actual evidence.
The 55 B77 dispositions remain one source/document audit complete, 42 partial,
nine local engineering results retained and three deferred. These are historical
v07 results, not new v08 passes.

B80 rechecks **340 B76 source/config/content/script hashes and 168 B76 evidence
hashes**. The sole intentional change among the 341 recorded source paths is
`.gitattributes`, which appends physical-byte preservation for the new v08
documentation and import. Existing native code, configuration, generated assets
and V07 data are unchanged. Newly staged V08 data has no loader or Build.cs
dependency. Native `v08_integration` remains rejected as an unknown profile.

Fresh `Scripts/Preflight.ps1` discovery passed: installed UE **5.8.4 CL58902052**,
supported MSVC 14.44.35229 and 14.51.36260, Windows SDK 10.0.26100.0 and .NET
Framework SDK 4.8. The selected historical build compiler is 14.44.35229.
[Portable preflight](evidence/B80/EnginePreflight.json) explicitly records
`buildVerified=false`. No fresh C++ build, cook, package, independent native
client session, performance run or human review was performed for B80. The last
native build/assets/40 rule tests and focused route/network results remain the
historical B76 evidence with their original limits. No native code changed here.

## Provenance reconciliation and spec revision

The supplied package had 38 files and lacked the `SOURCE_MANIFEST.json` required
by its checker. [The reconstructed manifest](SOURCE_MANIFEST.json) records all
38 original physical-byte hashes and verifies them against B77's measured
`preservation.v08UserDraftFiles`. Its `inputs` list hashes the two unchanged
source ZIPs. **The missing original manifest was not recovered; this is a B80
reconstruction, not historical approval.**

The manifest separately records adopted hashes and four deliberate spec edits:
README adoption/revision note, ability milestone wording, B80–B86 plan IDs and
the coding prompt's build sequence. Active JSON, templates, reference models,
checker and archives remain byte-identical to the supplied package. The source
documents' statements that no engine/repository was supplied describe their
creation context; this review supplies the actual implementation baseline.

ZIP member comparison against the existing release directories finds 78 v07
and 15 masterR7 files equal after LF normalization. Five archive-only provenance
or history members remain preserved inside the ZIPs; they are not missing
runtime dependencies. No prior release or master plan is overwritten.

## Native architecture and required adapters

| Boundary | Inspected implementation and v08 disposition |
| --- | --- |
| Profile/orchestration | `Framework/LDGameMode.cpp`: InitGame selects isolated v07/V05 paths; roster, owned records, life and ordered world outcomes stay native. Add schema8 at a fresh-match boundary in B81; no live conversion. |
| Combat | `Combat/LDCombatDirector`, `LDTacticalExecution`, `LDTacticalRules`, `LDTacticalFields` and source lifecycle files implement server release/costs/status/geometry. Reuse these; the module has no GAS dependency. All v08 operation enablement is pending revalidation. |
| Build/class | `Development/LDCharacterBuild` has 38 skill IDs, five v07 learned slots, nine presets over three legacy class adapters and no intrinsic trait layer. Keep legacy costs/learning/Witness; add exactly one immutable trait and typed mixed actions in B81. |
| Physical items | `Inventory/LDInventoryTypes`, `LDGearRules`, `LDInventoryDirector`: 24 identities, six bindings and three quick references. BindingMask is uint8 on both projected and canonical records; eleven bindings require coordinated widening, not a slot-count-only edit. |
| Transactions | Existing loadout requests carry build/inventory/item revisions; trade/crafting reserve owned items, and lineage remains server-only. B81 needs projected atomic gear/action drafts, source-reference invalidation and equipped-input exclusion. |
| Wire | Tactical schema7/wire3 (wire2 compatibility), loadout version1, build-plan schema7 with fixed 38 ranks/five slots. B80 reserves action wire4/loadout2/schema8 and a 16-bit binding mask as a plan only. Update serializers, server validation and owner projections together. |
| Views/UI | `Vision/LDWorldViewRelay`, `LDViewTypes`, native `UI/LDHUD`, development/inventory panels and Framework input files own filtered observation and controls. Retain Unknown enemy fields; add six source badges/four physical accessory actions without bag buttons. Existing public observations do not authorize full environment-grid replication. |
| Movement/world | Native CharacterMovement/Navigation plus region/world directors already support authored paths, finite world sources and scheduled claim ordering. B82 authors a static walkable river; later fields must not rebuild navigation. |
| Economy/routes | Economy/Inventory/World/Objectives directors preserve official creation, banking, trade, finite rewards, leases and source/sink ledgers. Keep the 642 world-gold cap, inherited overall budgets, four access routes and ten-second common claim. |
| Save/input migration | Existing local control preferences are not a persistent cross-version item save. Only a fresh-match development UI/save adapter may preview old bindings; conflicts become unbound instances. Identity, lineage and maximum cooldown endpoints survive; no grants. |

[B80_ADAPTERS.json](B80_ADAPTERS.json) is the machine-readable adapter inventory.
All 28 ability/40 item/12 recipe stable IDs are preserved. The nine legacy active
IDs plus None stay 0–9; retained abilities stay 10–37. The 24 added ability IDs
are reserved 38–61. Existing item enum values, including legacy utilities and
objective/material entries outside the 80-item count, retain their numbers; 40
new values are reserved after the native enum tail. These reservations do not
add native enum members or handlers.

All nine class presets map to one authored trait. Laborer retains Heave/Patch,
Rogue Pickpocket/Survey and Scholar Spark/Analyze, with existing 53-point v07
starts and utility costs. Legacy trait signature placeholders resolve to these
actual IDs in the staged payload. Arcanist's future new-match teaching kit uses
Frostline; existing learners do not lose Arc Bolt. Unknown class adapters reject.

## Import and acceptance disposition

`Scripts/SyncV08Data.py` validates the supplied structural schema, provenance,
IDs, operations, native ID preservation, recipes and class mappings, and runs
the supplied semantic/reachability/model checker before writing. It generates
`Content/Lockdown/Data/V08/catalog.json`, `ImportManifest.json` and the adapter
inventory deterministically. The payload contains all twelve schema8 data
documents, resolves legacy signature markers and hashes original source bytes.
Both runtime enable lists are empty; profile and every operation are disabled.
No unknown handler is claimed implemented, and no schema8 data enters the
closed schema7 catalogs. Native assets, behavior and legal acquisition remain
requirements of B81–B86.

The separate [working ledger](FeatureLedger.csv) has 363 rows: B80, 68 new native
cases, 55 inherited cases, 132 abilities/items and 107 recipes/traits/reactions/
operations/modifiers. C001 records the B80 source audit; every v08 native case
remains Not run. B77 dispositions/evidence are retained in separate columns;
C002/C003 and loadout expectations are explicitly rebased to six/four/eleven.
Templates and the supplied acceptance index remain unchanged.

[InheritedV06Acceptance.csv](evidence/B80/InheritedV06Acceptance.csv) preserves
all E01–E48 rows and links them to the inherited v08 ledger IDs.
[Deferrals.csv](evidence/B80/Deferrals.csv) preserves all V07-D01–D08 and their
V06-D01–D08 links and maps affected new cases. Human and long runs remain
deferred; focused independent-client/source/permission checks are still required
when implementing B81.

## Verification and next build

The supplied v08 checker passes **1,812 specification assertions and 18 reference
boundary tests**. Nine importer tests cover deterministic disabled output and
rejection of changed provenance, unknown operations/classes, duplicate IDs,
invalid integer fields, missing active binding, native renumbering and retained
recipe mutation. V07 model/import checks pass unchanged. Commands and captured
outputs are under [evidence/B80](evidence/B80/Audit.json).

```powershell
python -B docs/v08/tools/check_spec.py
python -B Scripts/SyncV08Data.py --check
python -B Scripts/TestV08Import.py
python -B Scripts/CheckV08Baseline.py
```

B81 must implement typed Learned/Gear action refs, six actions, seven body/four
accessory bindings, single traits, capped primary allocation, transactional
source removal, consumable-empty binding, death/cooldown semantics and the
matching owner UI/input/wire. Start with V08-001–015 and affected inherited
cases; prove stale/duplicate/source spoof requests spend nothing, Silence/Mute
classification is server-derived, no bag action exists and maximum cooldowns
survive rebinding/death. Introduce only supported sourced definitions in a
labelled profile. Freeze Source/Config during sequential Unreal build/probe runs
and keep the max4 compile cap. B82–B86 remain pending.
