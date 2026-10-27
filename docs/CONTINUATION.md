# Active continuation — v0.9 B90

Current native source is **4f8b91f**, based on **ebc19a5, v0.8 B86**, and implements the full-map spatial foundation, actual traversal, grounded river contact and native approach inspection. Read [approach inspection/current evidence](v09/B90_APPROACHES.md), [river contact](v09/B90_RIVER_CONTACT.md), [original foundation](v09/B90_IMPLEMENTATION.md), [baseline review](v09/B90_BASELINE_REVIEW.md), [working ledger](v09/FeatureLedger.csv) and [living game design](master/README.md).

## Current boundaries

B86 remains the playable short-map schema8 baseline: 52 abilities, 80 items, 20 recipes, nine presets, six mixed actions, eleven bindings, bounded fields and four earned Heart routes. Its original payloads, map assets, evidence and inherited deferrals remain unchanged.

B90 adds an independent full-map definition, closed spatial compiler, native tiled terrain/bridge/settlement builder, saved L_V09Full and a labelled standalone spatial preview. It carries 96 POIs, 117 routes, 144 building blocks and 24 spawn pads. The preview uses native character movement/navigation and creates no legacy match economy, population, inventories or objective rewards. It checks the saved definition against the current staged payload before admission. Fitter revision 2 raises three low decks to 300 cm for underpass clearance; the eastern deck stays at 400 cm. Layout revision 1 and the 59-file incoming source package stay unchanged.

The legacy 60×40 fog/region wire format is deliberately unchanged. Full-profile observation, interactive objects, interiors/stairs, routines/security, five-market accounting, AI progression and earned victory integration still require B91–B93. B94 owns full-duration, multiplayer stress and human pacing evidence.

The [native traversal increment](v09/B90_TRAVERSAL.md) now records 31 actual
journeys,15 border/terrace cases, all 11 settlement arrivals and two walked Heart
approaches. All pass; the longest interval without a POI anchor within 50 m is
339.067 s and remains a B91 functional-density risk. These are separate fixtures,
not a full competitive session.

The current contact increment adds grounded Wet and frozen-floor slow through
the shared v0.8 reducer. All 55 native contact cases pass, covering every named
crossing and all 17 route/river intersections; 42 actual exits verify residual
Wet and expiry. Dry decks remain unaffected by frozen water beneath them.
Frozen walking measures 294 cm/s and no Stun. Normal paid Frost/item commands,
full-map replication/privacy and life/resource dispatch remain B91 work; fields
in these fixtures are explicit diagnostic seeds. The complete fitter revision 2
replay passes 31 journeys and 15 boundary cases with zero falling or replans.

F5 opens a target-centered approach overview with wheel zoom and camera restore.
The new Approaches traversal lane walks all 23 incoming final sections to seven
key targets. All pass; 33 of 36 branch pairs are spatially distinct, and every
target has an alternative. The 288 cross-branch rays yield 129 clear, 159 out of
range and zero geometry blockers. Do not call these concealed approaches.
All 144 shell wall/opening calibrations and seven range/deck checks pass.
This static inspector is not full-profile unit observation/target authorization.

## Verification

Run native operations sequentially and keep Source/Config fixed during a run. Unreal 5.8.4 CL58902052, Win64 Development, MSVC14.44.35229 / SDK10.0.26100.0; use at most 3–4 compiler actions under current memory pressure.

~~~powershell
python -B Scripts/TestV09Import.py
python -B Scripts/SyncV09Data.py --check
python -B Scripts/SyncB84Data.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 3
./Scripts/BuildB90Map.ps1 -Validate
./Scripts/RunB90River.ps1
./Scripts/RunB90River.ps1 -Capture
./Scripts/OpenB90.ps1 -CheckWalk
./Scripts/RunTests.ps1
./Scripts/RunB82Check.ps1
./Scripts/RunB90Traversal.ps1 -Lane All
./Scripts/RunB90Traversal.ps1 -Lane Approaches
./Scripts/RunB90Traversal.ps1 -Lane Approaches -Capture
./Scripts/OpenB90.ps1
~~~

Rebuild this map with BuildB90Map.ps1; do not regenerate old maps. Native outputs and exact commands are retained in the portable B90 evidence/audit. Paths, estimates, walked times, fixtures and human evidence stay distinct. Root Artifacts contains local full logs and earlier failed development attempts.

## Next work

Finish the remaining B90 ledger cases before calling its full release gate complete. Contact fixtures and static spatial alternatives are now proven. Detailed district/interior fitting, full-profile observation, services and reset/source accounting still overlap B91/B92 integration. Fit cover along the actual native approaches without closing access; current range failures are not geometry cover. The report records this gate overlap; the structural preview does not fake those systems.

The next functional environment reference is Riverport public front → rear door → warehouse container → roof/stairs → grass escape. Reuse existing request/life/hold/inventory/receipt authority and maintain one collision/sight/local-nav mutation revision. Add no advertised cosmetic-only verbs or unbound item shortcuts.

Preserve all B86/B77/B67 named deferrals, current starter kits and the audited real Witness check. Continue actual evidence without inferring human balance, full-session pacing, packaged dedicated-server support or old-executable compatibility from focused local checks.
