# Continuation — v0.8 B80 complete; B81 slots/traits next

25 October 2026. **B80 audit/import is complete.** The user requested consistent
Bxx build IDs: B80 Audit/import, B81 Slots/traits, B82 River core, B83 Fields,
B84 Catalog/acquisition, B85 World integration and B86 Native usability.
Read the [B80 review](v08/B80_BASELINE_REVIEW.md),
[active specification](v08/README.md), [build plan](v08/11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md),
[adapter inventory](v08/B80_ADAPTERS.json) and [working ledger](v08/FeatureLedger.csv).
Commit authorized; no push requested.

## Actual current progress

- Audit HEAD `7d9d66d` (B77); native runtime/source/assets baseline `ddf7be5`
  (B76). v0.7 is closed with scoped results and named deferrals, not a fully
  verified distribution. [B77 acceptance](v07/B77_ACCEPTANCE.md) remains intact.
- All 38 original v08 input hashes match the B77 inventory. The missing
  SOURCE_MANIFEST was explicitly reconstructed; four spec edits adopt B80–B86
  terminology. Active data/templates/archives/models are unchanged.
- `Scripts/SyncV08Data.py` produces the isolated, disabled schema8 payload and
  hash manifest in `Content/Lockdown/Data/V08/`. All 52 abilities/80 items/20
  recipes/9 traits/10 reactions are inventoried. Retained 28/40/12 IDs and native
  legacy utilities/materials/objectives stay distinct. Legacy trait signature
  placeholders resolve through actual starter IDs. New numeric IDs are reserved,
  not implemented native enum entries.
- B80 checks pass: 1,812 spec assertions/18 reference tests, nine focused import
  tests, 340 retained B76 source hashes and 168 evidence hashes, prior V07 model
  and four import checks. `.gitattributes` intentionally appends preservation
  rules for v08. No existing native code/config/assets changed.
- Fresh preflight confirms UE5.8.4 CL58902052 and available supported tools.
  No fresh native build/cook/package/client/performance/human run at B80. The
  last native engineering evidence is still B76 with its stated limits.
- Working ledger: 363 rows including 68 new and 55 inherited acceptance cases;
  v08 native cases remain Not run. E01–E48 and all V07-D01–D08/V06-D01–D08
  dispositions are mapped and preserved under `v08/evidence/B80/`.

## Implement B81

1. Read 00, 01, 02, 03 and 09 of v08 plus the B80 review. Reuse the native
   framework; no GAS rewrite or browser replacement. Implement an isolated
   schema8 profile at InitGame/fresh match only. It does not exist at B80:
   `v08_integration` currently rejects as unknown, and no runtime loader/module
   dependency reads V08 data. Never feed it through schema7 catalogs.
2. Add typed Learned(ability) / Gear(instance, active-definition) references to
   six shared actions, seven body bindings and four physical accessory bindings.
   Accessories act from their own binding; there is no independent quick bar
   or backpack combat activation. Basic/world/book interactions stay distinct.
3. Widen **both** projected/canonical item BindingMask fields from uint8 for
   eleven bindings. Update bounded build/loadout/tactical serializers, request
   validation, replay receipts, owner views and UI together. B80 proposes
   schema8/action wire4/loadout2/16-bit mask; these values are not implemented.
   Preserve v07 wire3/version1 and V05 regressions. Source category is derived
   server-side: Silence blocks all learned skills; Mute blocks gear/accessory
   actions even when a body item occupies a shared slot.
4. Commit projected gear/actions atomically with life/build/inventory/item
   revisions. Quiet-unbind invalidates dependent bar actions. Reject equipped
   recipe/trade inputs. Consume one bound identity and empty that accessory;
   never auto-refill. Retain maximum instance/owner-family/learned cooldowns,
   lineage and item identity through death/drop/trade/rebind. Consumables and
   objective relics drop while ordinary bound equipment retains. Keep all
   equipped instances inside the 24-instance bag and existing weight budget.
5. Implement exactly one immutable nonbreakable trait per nine presets; no
   subclass stacking or free signature button. Keep legacy starting skills,
   economic utility costs and Witness checks. Arcanist new matches teach
   Frostline only when that operation is actually supported; keep unavailable
   definitions/presets explicitly disabled until their dependencies are ready.
6. Allocate primary bonuses in eleven-binding order with +2/piece/attribute,
   +3/attribute and +6 total caps; two-hand instance counts once. Preview excess,
   migration conflicts and source-dependent action removal. Q/W/E/R/D/G and
   accessories1–4 require actual collision/focus checks; A/F/T/S retain meaning.
7. Start with V08-001–015 and affected inherited cases. Prove authority/source
   invalidation, Silence/Mute, stale/duplicate/spoof requests, cooldown persistence,
   no bag activation and native owner UI through focused independent clients.
   A data import/model pass does not satisfy these exits. Human and long runs
   remain deferred; do not rerun them merely to close a milestone.

## Reproduction and preservation

```powershell
python -B docs/v08/tools/check_spec.py
python -B Scripts/SyncV08Data.py --check
python -B Scripts/TestV08Import.py
python -B Scripts/CheckV08Baseline.py
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
```

The B80 checker is a frozen audit checkpoint: later native implementation
intentionally invalidates its baseline comparison. Preserve recorded evidence;
do not rewrite historical hashes to hide migration. Use the importer check for
current generated contract identity, updating adoption provenance deliberately
when active spec bytes change. Keep templates separate from the working ledger.

Unreal operations run sequentially with Source/Config frozen. Pinned Win64
Development UE5.8.4 CL58902052, selected MSVC14.44.35229 / SDK10.0.26100.0,
max4 build actions. Existing `BuildEditor.ps1 -MaxParallelActions 4 -Gather`
and `BootstrapAssets.ps1 -Mode Validate` remain available. Explicit v07 opens
`L_V07Region`; frontend/V05 stay on the retained map. No current distribution
or dedicated-server/reconnection release acceptance is implied.

B82 must author a permanently navigable river before field simulation. B83
adds bounded fields/reactions with authorized views; B84 completes real catalog
handlers/acquisition; B85 revisits all four normal routes and conservation;
B86 completes native usability/evidence. Keep 30s warning/60s access windows,
finite rewards and ordered ten-second claim; Wet/slow/damage alone do not
cancel a surviving stationary claim. Applied direct-Frost Stun does.

Preserve all prior version records and original archive bytes. B77's old
Seal-trade fixture remains incompatible with V05 economy: use earned B74
trade/return evidence rather than granting diagnostic Seals. Dead/disconnected
reward escrow is not reconnection support. B76 conservative changed-pose hold
cancellation is not native movement resimulation. Human/pacing/privacy/race/
package/performance gaps retain their named deferred dispositions.
