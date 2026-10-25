# Continuation — v0.7 closed; v0.8 audit/import next

25 October 2026. v0.7 development is closed at B77 under the user's instruction
to keep checks minimal, avoid lengthy tests and defer human review. The runtime
baseline is **`ddf7be5` (B76)**; B77 changes documentation/evidence only. Read
[v0.7 closeout](v07/B77_CLOSEOUT.md), [final acceptance](v07/B77_ACCEPTANCE.md)
and [portable audit](v07/evidence/B77/Audit.json). This is the sole active handoff;
keep `V07_TRANSITION.md` deleted. Commit authorized; no push requested.

## Immediate v0.8 Stage0

The supplied [v0.8 package](v08/README.md) exists as38 untracked user files.
B77 did not modify or stage it. Its statements that no repository/engine/status
was supplied describe its original design context, not this checkout: actual
source, pinned engine and B71–B76 evidence are available here.

1. Resolve package provenance first. `python -B docs/v08/tools/check_spec.py`
   currently fails because `docs/v08/SOURCE_MANIFEST.json` is missing. Restore
   it from the canonical package or explicitly reconstruct current archive
   provenance with measured hashes and a documented origin; never label newly
   computed hashes as historical approval. B77 Audit records all38 input hashes,
   including the two supplied reference ZIPs. Run the lightweight checker after
   reconciliation; no v0.8 model pass is claimed yet.
2. Deliberately inventory/adopt the complete v0.8 prose/data/templates as the
   new contract, with original package hashes and adapters. Leave v0.7's93
   adopted files and historical reports intact. Map52 abilities/80 items/20
   recipes, nine traits and ten reactions to the actual stable IDs: retain28
   abilities/40 items/12 recipes and audit legacy utilities/book wrappers/classes
   separately. Do not assume the package's unknown-baseline statements mean
   the existing implementation is absent.
3. Map its68 new and55 inherited acceptance cases to the B77 matrix and
   V07-D01–D08/V06-D01–D08. Superseded slot counts belong to the old profile;
   retain behavior/authority/accounting invariants in the new one. Partial/unrun
   acceptance stays partial/unrun. Human and long runs remain deferred until
   the user changes direction; use proportionate checks for actual source edits.
4. Audit the existing directors, request/view serialization, native widgets,
   inventory reservations and source lifetimes before choosing adapters. No GAS
   rewrite or replacement browser game is needed. Create an isolated v0.8
   profile/import only during implementation; it does not exist yet. Never load
   schema8 data through the closed schema7 catalogs or convert a live match.

## Migration boundaries

| Actual v0.7 baseline | Proposed v0.8 change; not implemented |
| --- | --- |
| Five learned skill slots; item effects use binding plus three quick references | Six mixed learned/body-gear actions, plus four physical accessory bindings/actions; no independent quick bar |
| Six physical gear bindings,24-instance bag | Seven body plus four accessory bindings; equipped instances remain inside24; one two-handed instance spans two bindings |
| Class starter templates; no permanent intrinsic-trait layer | Exactly one nonbreakable intrinsic trait per audited class; signatures remain replaceable learned actions |
| Wire3 source requests, server revision/life/geometry checks; public eight-field observation | Server-derived learned/gear/accessory source kinds and atomic reference invalidation; version request/view contracts explicitly |
| Native navigation, smoke/Ember and bounded tactical fields | Static walkable river ford/dry detour; bounded environment cells/reactions, no elemental navmesh regeneration |
| Shared ordered10s claim; damage/slow/root/silence/mute preserve it | Direct Frost on Wet may stun600ms with victim-wide6000ms lock; Frozen ground slows30%, never contact-stuns; due stun still precedes claim completion |

At a fresh match/UI boundary, preview and revalidate equipment/action migration;
retain stable item identity/cooldown/lineage and spill conflicting references to
unbound inventory without free items. Body gear activations consume a shared
action slot; accessories use their own physical slot. Silence blocks learned
actives and Mute blocks item-sourced ones; derive source classification on the
server. Reuse life/build/inventory revisions and replay-safe transaction commits.

Implement in the package's stages: slots/traits, small river core, bounded fields,
remaining catalog/acquisition, world integration, native usability. Expose only
implemented/sourced definitions in a labelled development profile. The river
slice must keep player and AI navigation traversable before/during/after freeze;
world fields must use authorized fog/smoke views rather than full-grid replication.
Keep30s warning/60s windows, finite sources and all four routes; surface damage
alone cannot cancel a surviving stationary claim. Do not fabricate historical
native trajectories when extending the scheduled authority clock.

## Baseline locations and reproduction

- Server orchestration: `Source/Lockdown/Private/Framework/LDGameMode.cpp`.
- Actions/effects/costs/source lifecycle: `Source/Lockdown/Private/Combat/`.
- Builds/preparation: `Development/`; owned instances/trade/acquisition: `Inventory/`;
  accounting/services/production: `Economy/`, under the same Private directory.
- Region/encounters/leases/ordered outcomes: `World/`, `Objectives/`, `Region/`;
  native movement/pathing: `Character/`, `Navigation/`.
- Authorized views: `Vision/`; owned native UI/input: `UI/` and Framework player files.
- Staged schema7 data: `Content/Lockdown/Data/V07/`; closed sync scripts and native
  generated catalogs/maps are inventoried in B76 SourceManifest/DataImport.

```powershell
./Scripts/OpenStandalone.ps1 -Profile v07_integration
./Scripts/OpenStandalone.ps1 -Profile v06_regression
python -B docs/v07/tools/check_spec.py
python -B Scripts/SyncV07CombatData.py --check
python -B Scripts/SyncV07EquipmentData.py --check
python -B Scripts/SyncV07AcquisitionData.py --check
python -B Scripts/SyncV07WorldData.py --check
```

Explicit v07 opens `L_V07Region`; default frontend/V05 regression stay on the
retained map. Existing optional build/asset commands are `BuildEditor.ps1
-MaxParallelActions 4 -Gather` and `BootstrapAssets.ps1 -Mode Validate`. Run Unreal
operations sequentially and freeze Source/Config during each. UE5.8.4 CL58902052,
Win64 Development, MSVC14.44.35229 / SDK10.0.26100.0; compile cap4. No fresh
package or full release acceptance exists; old distributions predate this source.

B76 already records four normal-route demos,19 boundary cases, focused100/150ms
world clients,40 rules and affected regressions, with33 independent ledger
replays. B77 only verifies provenance/imports/models and replays four historical
route snapshots. Do not rerun long suites just to close this handoff.

## Preservation and unresolved evidence

B77 preserves all93 adopted v0.7 files. The pre-existing edit in
`v07/09_VICTORY_AND_MATCH_STATE.md` remains excluded from commits; the other92
match adoption. The supplied untracked `v08/` package stays unchanged and outside
the closeout commit until deliberate adoption. Preserve current user bytes when
resolving its missing provenance manifest.

V07-D01–D08 name deferred cross-feature networking/privacy, cold package,
restart/performance, physical/rendered input, progression/last-hit source loss,
combined authority races, normal class/tactical journeys and human/pacing work.
Dead/disconnected reward escrow attribution exists but is not reconnection
support; its full adversarial matrix is unrun. B76 conservative hold cancellation
on ambiguous changed pose is not historical native movement resimulation.

All48 inherited v0.6 rows and V06-D01–D08 remain traceable. The old B56 Seal-trade
fixture is incompatible with V05 economy: use B74's earned trade/return evidence;
do not add diagnostic Seal grants to make that obsolete fixture pass. Human
review remains deferred; no invented participants, balance or performance pass.
