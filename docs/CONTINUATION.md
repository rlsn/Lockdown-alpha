# Active continuation — B90 complete; B91 next

Native source **7e96ece** completes the B90 world skeleton at its stage-owned spatial boundary, based on
**ebc19a5, v0.8 B86**. Read [completion and current evidence](v09/B90_COMPLETION.md),
[working ledger](v09/FeatureLedger.csv) and [living game design](master/README.md).
The completion audit pins the exact native source commit, retained asset bytes,
commands and raw reports. Earlier foundation/traversal/river/approach reports
remain historical evidence; their hashes and supplied source package are unchanged.

## Current boundaries

B86 remains the playable short-map schema8 baseline: 52 abilities, 80 items, 20 recipes, nine presets, six mixed actions, eleven bindings, bounded fields and four earned Heart routes. Its original payloads, map assets, evidence and inherited deferrals remain unchanged.

B90 adds an independent full-map definition, closed spatial compiler, native tiled terrain/bridge/settlement builder, saved L_V09Full and a labelled standalone spatial preview. It carries 96 POIs, 117 routes, 144 building blocks and 24 spawn pads. The preview uses native character movement/navigation and creates no legacy match economy, population, inventories or objective rewards. It checks both the source and reflected saved-definition properties before admission. Fitter revision 3 retains the raised bridge decks and adds exact settlement/service metadata and wall-clipped road paint. Layout revision 1, seed 9001 and the 59-file incoming source package stay unchanged.

The legacy 60×40 fog/region wire format is deliberately unchanged. Full-profile observation, interactive objects, interiors/stairs, routines/security, five-market accounting, AI progression and earned victory integration still require B91–B93. B94 owns full-duration, multiplayer stress and human pacing evidence.

The completed increment binds 36 service seats to their selected buildings and
11 settlement identities. F6 shows public districts, native hub paths and civic
policy rings; those rings grant no live observation or protection. All 216
doorway capsule lanes and 72 actual service/through-shell walks pass, with zero
road/wall overlaps. F9 performs fresh B90 level travel; F10 opens a fresh v0.8
lobby. The reset fixture proves three worlds discard actors/contact state and
reject stale generations, then runs the v0.8 source ledger in the same process.
Two independent fitter3 generations and a saved reopen agree on geometry/pads.

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
in these fixtures are explicit diagnostic seeds. The complete fitter revision 3
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
./Scripts/RunB90Structure.ps1
./Scripts/RunB90Closure.ps1
./Scripts/RunB90River.ps1
./Scripts/RunB90River.ps1 -Capture
./Scripts/OpenB90.ps1 -CheckWalk
./Scripts/RunTests.ps1
./Scripts/RunB82Check.ps1
./Scripts/RunB90Traversal.ps1 -Lane All
./Scripts/RunB90Traversal.ps1 -Lane Approaches
./Scripts/RunB90Traversal.ps1 -Lane Approaches -Capture
python -B Scripts/CheckB90Completion.py docs/v09/evidence/B90Completion
./Scripts/OpenB90.ps1
~~~

Rebuild this map with BuildB90Map.ps1; do not regenerate old maps. Native outputs and exact commands are retained in the portable B90 evidence/audit. Paths, estimates, walked times, fixtures and human evidence stay distinct. Root Artifacts contains local full logs and earlier failed development attempts.

## Next work

Proceed to **B91 functional environment**. B90's twelve spatial cases pass; the
completion report separately records later gameplay dependencies in composite
acceptance sentences. B92 must still initialize and reset the full-profile
economy and reconcile its five markets. Fit useful opportunities along the
339-second geometric POI-anchor gap and cover along actual approaches without
closing access. The retained time-weighted travel heat plot is a fixture
diagnostic, not a prediction of population traffic or human pacing.

The next functional environment reference is Riverport public front → rear door → warehouse container → roof/stairs → grass escape. Reuse existing request/life/hold/inventory/receipt authority and maintain one collision/sight/local-nav mutation revision. Add no advertised cosmetic-only verbs or unbound item shortcuts.

Preserve all B86/B77/B67 named deferrals, current starter kits and the audited real Witness check. Continue actual evidence without inferring human balance, full-session pacing, packaged dedicated-server support or old-executable compatibility from focused local checks.
