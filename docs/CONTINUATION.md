# Continuation — B81 implemented; B82 river core next

26 October 2026. B81 implements the native v08_integration development profile
on B80 (`aeffed2`). Read [B81 implementation](v08/B81_IMPLEMENTATION.md),
[engineering evidence](v08/evidence/B81/Summary.json),
[working ledger](v08/FeatureLedger.csv), [active spec](v08/README.md) and
[B80–B86 plan](v08/11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md).
Commit remains authorized; no push requested.

## Current implementation

- Six typed Learned/Gear actions; seven body/four physical accessory bindings;
  no independent quick bar or backpack activation. Canonical and owner binding
  masks are 16-bit; all eleven bound instances count inside 24.
- Versioned projected drafts commit equipment/actions atomically through native
  quiet/stationary preparation. Source unbind clears dependent actions. Learned
  actions are Silence-blocked; body/accessory actions are Mute-blocked. Legacy
  Step now follows the v08 learned-skill Silence rule.
- Source identity, natural gates, revisions, reservations, weight, primary caps,
  instance/owner-family cooldowns and death policies remain authoritative.
  Consumable use empties its binding without refill; ordinary bound gear retains
  on death, consumables/objectives drop, explicit return refills HP/MP only.
- One immutable preset-derived trait, with native capacity/Brace arc/clue/sight/
  assembly discount/decoy hooks. Arcanist/Scholar field-duration rules await
  actual B82/B83 environment integration; do not call those field effects proven.
- Native owner HUD, inventory and preparation use six/four/eleven with source
  badges and full projected binding/stat-cap preview. Defaults Q/W/E/R/D/G and
  accessories1–4 migrate without overwriting valid v07 preferences.
- Enabled slice: 27 retained abilities, 54 items, eight presets. Arcanist preset5
  requires Frostline and remains unavailable. Ember Line adds a v08 Burning
  surface, so it stays hidden until B83; its v07 implementation is preserved.
  Other elemental/advanced modifiers, remaining items and eight new recipes are
  hidden until supported. Full v08 still requires all 52/80/20 definitions.
- Separate schema8 payloads/compiled hashes live in Content/Lockdown/Data/V08.
  Runtime does not read docs or load schema8 through schema7. B80's complete
  disabled catalog, reservations and original package/provenance stay frozen.
- Native Editor build and 41 compiled rule tests pass, plus focused independent
  v08 clients and the separate retained v07 B73 loadout fixture. Portable results
  and independent ledger replay are in evidence/B81; retain exact scope.

## B82 implementation boundary

1. Author a static river floor connected to both banks plus a dry detour in the
   native region. Player and AI must cross on the same graph before, during and
   after Frozen. No navmesh/collision changes from elemental state, swimming or
   drowning. Keep the v07 region/profile separately reproducible.
2. Add authoritative water contact: immediate Wet on crossing, 100ms refresh,
   3000ms residual (1500ms for the authored Waders modifier when enabled).
   Frozen river retains underlying water contact; Frozen ground applies 30%
   slow (15% Treads when enabled), never contact stun.
3. Implement real Frostline/direct Frost and required small water sources with
   actual MP, slots, books/items and geometry. Direct hostile Frost on Wet can
   attempt exactly 600ms Stun with a contender-wide 6000ms readiness endpoint.
   Field contact/ticks never trigger it. Guard/immunity consume Wet and set the
   lock on valid attempts; spell block prevents the attempt entirely. No self,
   protected official, boss/tower hard-control bypass.
4. Keep reaction readiness through death; do not put it in an ordinary dispellable
   status. Revalidate original life at impact; deduplicate cast/victim-life.
   Exact readiness boundary is inclusive; denied attempts never extend it.
5. Due applied Stun precedes claim completion; Wet/slow/nonlethal damage alone
   do not cancel a surviving stationary claim. Guarded/suppressed Stun does not
   cancel. Preserve four-route endpoints, deadline/tie order and existing ledgers.
6. Extend source IDs using B80 reservations, bounded wire/digest contracts and
   both native owner/client projections. B81 currently has 38 rank identities;
   adding Frostline requires coordinated native enum/rank/import changes. Do not
   regenerate or rewrite historical B80 payloads/manifests to hide migration.
7. Start with V08-016–027 plus applicable claim/AI/source cases. Use real native
   player/AI crossing and independent-client receipts, not Python models as
   gameplay proof. B83 owns the complete bounded grid/reaction/reservation/
   privacy service; do not expose unsupported ops through an enabled catalog.

## Commands and remaining acceptance

```powershell
python -B Scripts/SyncB81Data.py --check
python -B docs/v08/tools/check_spec.py
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/RunTests.ps1
./Scripts/RunB81Check.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/OpenStandalone.ps1 -Profile v08_integration
./Scripts/OpenStandalone.ps1 -Profile v07_integration
```

Run Unreal operations sequentially with Source/Config frozen. UE5.8.4
CL58902052, Win64 Development, MSVC14.44.35229 / SDK10.0.26100.0, max4 compile
processes. The native v08 B81 slice uses L_DevelopmentRegion. Explicit v07 opens
L_V07Region and retains the B76 four-route world; frontend/V05 remain separate.

The working ledger has 364 rows: B80/B81, 68 new/55 inherited cases, definitions
and contract dependencies. B81 evidence is focused engineering acceptance;
new/native cases beyond that scope remain partial/not run. All E01–E48 and
V07-D01–D08/V06-D01–D08 remain mapped and preserved. Human review, physical/
rendered input matrices, cold package, long sessions and full cross-feature
races/privacy remain pending/deferred. No reconnection or fresh distribution
is claimed. Future stages are B83 fields, B84 full catalog/acquisition, B85
world integration and B86 native usability/release evidence.
