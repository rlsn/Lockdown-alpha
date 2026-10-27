# B90 — native approach inspection

27 October 2026 · Native source **4f8b91f**.

Continues the fitter revision 2 [river/contact work](B90_RIVER_CONTACT.md).
The saved map, incoming layout/catalogs and all existing asset bytes are unchanged.

## Inspection behavior

F5 in the standalone spatial preview draws the final 60 m of each incoming
native path to the nearest rare target: three ruins, the boss, the Heart forge,
the ordinary Ironworks forge and the Heart. Shorter incoming paths retain their
whole length. Branches have distinct colors and public origin labels. These
are native navigation paths, including open-terrain shortcuts, rather than
straight lines copied from the authored strategic graph.

F5 opens a camera overview centered on the nearest target, with a target marker.
The mouse wheel adjusts overview zoom. F5 again restores the previous camera
distance and offset; ordinary movement controls remain available.

The last section is replanned from its locally fitted start before inspection:
Recast can shorten a route when given a new starting point. Samples resolve the
local navigation floor before tracing ground, rather than interpolating height
between distant corners. This keeps the drawing, ray endpoints and native walk
on the same local route. The checker requires recorded walk samples to remain
within 1.5 m of the inspected centerline and reconciles local planned lengths.

For each branch pair, the inspector compares points 9, 18, 24 and 30 m of
remaining path distance from the target, when that distance exists on both
paths. It draws the 18 m comparison ray in green for clear geometry, red for a
geometry blocker and yellow for outside ordinary sight radius. The underlying
report separately evaluates the 24 m public-watch radius. A range failure is
never labelled cover or occlusion.

A pair qualifies as a spatial alternative when its centerlines are at least
6 m apart at **both 18 m and 30 m remaining**. This is an explicit conservative
inspection metric for two 3 m corridors, not a balance rule, guaranteed cover,
minimum building clearance proof or a claim that defenders cannot observe both.
Each target must have at least one qualifying pair; the report retains every
pair, including merged approaches.

## Static geometry boundary

The inspection helper traces between floor-relative eye points 120 cm above
the sampled floors. It uses existing native sight blockers and ground/deck
collision, so raised buildings are checked at their actual band and a bridge
deck can block a ray between upper and lower levels. Own/public radii remain
18/24 m regardless of elevation. A ramp acquires no artificial opaque wall.

This helper inspects public static geography in the existing standalone mode.
It is **not** a unit observation/targeting authority and has no network relay,
private actor handles, minimap units or targeting requests. Tagged one-way
ledges, smoke/steam, grass concealment, observation audiences, mutation revisions
and restricted-client serialization remain B91 work. The legacy 60×40 vision
wire format and v0.8 observer/combat services are unchanged. No claim of complete
full-profile sight implementation follows from a static ray.

The building calibration checks a wall ray and the front/rear opening axis for
every primitive shell. An open axis in one of the 36 selected shells does not
claim a functional door, finished interior or stair. Those B91 acceptance gates
remain open.

## Native evidence method

`RunB90Traversal.ps1 -Lane Approaches` derives all 23 incoming branches from the
checksum-closed compiled map, records their native paths and samples their final
sections against saved collision. A labelled placement begins each case, after
which ordinary CharacterMovement/navigation walks to the target at 420 cm/s.
No ability, teleport during walking or world mutation is used. The existing
traversal trace checker validates arrival, continuity, distance, speed and world
bounds. The approach checker independently reconciles target/route identities,
polyline interpolation, pair separation, range classification and actual walks.

The suite also checks all 144 shell wall/opening axes, inclusive 18 m range,
rejection beyond range, invalid radius and the four bridge-deck vertical rays.
The rendered lane is a separate antechamber-to-Heart walk with F5 enabled and a
native screenshot; it does not substitute for the complete headless run.

~~~powershell
./Scripts/BuildEditor.ps1 -MaxParallelActions 3
./Scripts/RunB90Traversal.ps1 -Lane Approaches
./Scripts/RunB90Traversal.ps1 -Lane Approaches -Capture
./Scripts/RunTests.ps1
./Scripts/BuildGame.ps1 -MaxParallelActions 3
~~~

Native operations run sequentially with Source/Config fixed. Original B90,
traversal and river evidence remains immutable. Long-duration, population,
economy, mutation, restricted-client and human pacing gates stay open.

## Results and design consequence

| Check | Result |
| --- | --- |
| Targets / incoming branches | 7 / 23; every final-section walk passes |
| Pair classification | 33 distinct pairs; 3 merged pairs; each target has an alternative |
| Cross-branch static rays | 129 clear, 159 out of range, **0 geometry blocked** |
| Shell calibration | 144 wall rays block; 36 selected-shell opening axes clear; 108 closed axes block |
| Boundary geometry contract | All 7 checks pass, including 4 bridge deck rays |
| Actual walking | 318.850 aggregate simulated seconds; no falling or replans |
| Largest recorded walk/inspected-line deviation | 2.451 cm |
| Rendered Heart approach | Separate 10.683 s walk passes; native overview image inspected |
| Existing traversal compatibility | 15 boundary cases and ordinary Heart-route lane pass |
| Native rules | 51 pass |
| Editor / Game builds | Win64 Development passes on pinned Unreal 5.8.4 |

These are **open alternatives**, not concealed ambush routes. A range rejection
does not demonstrate an occluding wall, tree or ledge. The retained report
identifies merged pairs instead of counting every strategic edge as a separate
local corridor. B91 cover fitting must work on these measured native paths and
preserve permanent access. These observations do not establish defender balance,
public watch coverage or human encounter pacing.

The 159 radius failures short-circuit geometry tracing. Zero blocked means no
blocker among the 129 in-range casts, not a global absence of cover throughout
the surrounding area. Out-of-range geometry is not certified by this test.

All incoming source files, compiled payload and 148 saved full-map assets remain
unchanged. Portable reports, traces, calibration, screenshot and build evidence
are retained in [the approach bundle](evidence/B90Approaches/README.md).
