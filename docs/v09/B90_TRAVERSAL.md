# B90 — native journeys and movement boundaries

27 October 2026 · Source **b96c329** · Based on **c9b0529**.

This increment replaces several remaining travel estimates with actual native
CharacterMovement observations. It does not advance into B91's world-object
interactions or close the complete B90 release gate. The supplied specification,
catalogs, generated map/collision assets and historical B90 evidence are unchanged.

## Implementation

RunB90Traversal launches the isolated full-map spatial mode and runs:

- All 20 prescribed atlas journeys.
- Each outer city's two nearest towns: eight additional journeys.
- Capital to Highfort, completing arrival coverage across all five cities and
  six towns.
- Western ford to Heart and dry flank to Heart, demonstrating two actual
  ordinary-movement approaches.
- Eight compass/corner perimeter locations and seven macro-terrace aprons.
  Each runs ordinary walking, the existing native 360 cm root-motion dash, and
  floor-following swept push fixtures.

Each case begins with a labelled placement. Journeys then use the same preview
navigation controller as a normal right-click, with no teleport, movement
ability, object mutation, extra movement speed or mid-route placement. Boundary
dash/push deliberately exercise physical movement; they do not pretend to pay
for a learned ability or test combat control/access/interrupt semantics.

The preview controller now handles coincident destinations as arrival, rejects
out-of-world goals and reports arrival/blocked states. A stalled path gets at
most two replans before it stops with truthful feedback. The passing journey
set needed zero replans; it does not certify stale-navigation mutation races.

Trace output records position, speed, walked distance, nearest POI and static
water geometry every simulated second, plus arrival. Boundary samples and each
swept-push step record endpoints and capsule-overlap checks. Partial reports
are saved after cases, so failures remain inspectable. The independent checker
verifies expected routes/cases, fixed timestep, trace continuity, speed bounds,
arrival, POI distances, gap counters and safe boundary endpoints.

## Results

| Check | Outcome |
| --- | --- |
| Actual journeys | 31/31 passed; every settlement was a reached destination |
| Border / terrace cases | 15/15 passed |
| Navigation contract | Coincident arrival, outside-world rejection and closed-shell rejection passed |
| Falling / navigation replans | Zero falling seconds and zero replans on journeys |
| Simulation | Fixed 1/60 s native ticks; 6,149.867 s aggregate walking |
| Wall execution | 202.420 s; accelerated standalone fixture, not a match-duration/performance claim |
| Western ford → Heart | 89.500 s |
| Dry flank → Heart | 35.667 s |
| Short antechamber → Heart regression | 10.700 s |
| Garden bridge entry regression | 2.583 s |
| Editor / Game builds | Passed, Win64 Development on pinned Unreal 5.8.4 |
| Native rules | 49 passed, zero failures |

The boundary query uses the actual 28/90 cm capsule with a 1 cm contact tolerance
for overlap classification. Native swept movement retains its full capsule.
The perimeter blocks walking, dash and outward push. The seven terrace cases
cover existing continuous terrain aprons, not future B91 vertical ledge tags or
portable traversal. All checked endpoints stay inside the playable ±118,000 cm
center limit and outside blocking overlap.

The **102.5 minutes are summed across separate placed fixtures**. This is not a
60/75/90-minute competitive session, an economy run, a soak or human observation.
Only geometric river contact is recorded; Wet, frozen surfaces and reaction
ordering remain unimplemented in this preview.

## Measured travel and density findings

[MeasuredTravel.csv](evidence/B90Traversal/MeasuredTravel.csv) keeps strategic
graph lengths, graph estimates, native path lengths and measured walking
separate. For example:

| Journey | Graph estimate | Actual baseline walk |
| --- | --- | --- |
| Highfort → Crownreach | 266.24 s | 265.35 s |
| Lumen Archive → Crownreach | 323.72 s | 252.67 s |
| Riverport → Crownreach | 369.73 s | 263.07 s |

Ordinary native navigation can take legal open-terrain shortcuts instead of
following every strategic graph junction. Do not tune income or warning reach
using graph times as though they were measured player travel.

The empty-interval proxy means **no addressable POI anchor within 50 m**. The
radius is an explicit diagnostic assumption, not a new interaction range or
reward rule. It does not count scenery, cover decisions, future functional props,
residents, rivals or hazards. The largest observed stretches are:

| Journey | Longest anchor-free interval |
| --- | --- |
| Highfort → Lumen Archive | 339.07 s |
| Lumen Archive → Ironworks | 301.10 s |
| Riverport → Crownreach | 188.52 s |
| Southern ruin → antechamber | 184.02 s |

These are concrete density risks relative to the intended 45–90-second major
choice rhythm. B91 placement review should inspect the actual trace corridors,
including off-road shortcuts, before adding acreage or increasing movement
speed. Add or move meaningful existing opportunities/cover through an explicit
fitted-layout change, then remeasure. The proxy alone cannot establish whether a
finished encounter-rich route is boring or balanced.

The baseline western ford trip also exceeds the 80 seconds available to arrive
and finish a new ten-second claim within a 30-second warning plus 60-second
opening. Local forward staging matters; a public warning does not guarantee
claim feasibility from every approach. The activator's antechamber walk still
meets its ≤15-second target. No warning/window tuning changed here.

## Reproduction

```powershell
./Scripts/BuildEditor.ps1 -MaxParallelActions 3
./Scripts/RunB90Traversal.ps1 -Lane All
# Focused debugging lanes; these do not replace the full run:
./Scripts/RunB90Traversal.ps1 -Lane Bounds
./Scripts/RunB90Traversal.ps1 -Lane Routes -Trip highfort__capital
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Traversal
./Scripts/BuildGame.ps1 -MaxParallelActions 3
./Scripts/RunTests.ps1
```

Native operations run sequentially with Source/Config fixed. The runner uses
NullRHI, benchmark execution and fixed 60 Hz simulation. Portable raw output,
source hashes, commands and exact local artifact paths are in
[Audit.json](evidence/B90Traversal/Audit.json). Earlier failed development builds
remain local and are not counted as passes.

## Remaining work

The ledger now records the actual travel and physical boundary coverage.
Functional density, sightline encounters, Wet/frozen contacts, service bindings,
full-profile reset/accounting, ordinary paid combat movement, multiplayer and
human pacing remain open. B90's complete gate remains partial; no source
acceptance row or original template was rewritten to imply otherwise.
