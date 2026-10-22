# B53 HUD, observation and minimap

22 October 2026. Predecessor: `9f4c58f` (B52). This checkpoint implements the
native HUD/inspector/minimap. B54–B58 remain required for full v0.5.

## Behavior and authority

`LDHUD` replaces the old V05 HUD with an anchored bottom band: minimap, selected
subject inspector and permanently owned commands. HP/max HP, XP/level, gold,
unspent points, protection/return and gold loss come from actual own state. Four
equipped skill buttons show live mappings, rank, Empty/Ready/Armed/Pending/Active/
Cooldown/Blocked states and definition-based tooltips. Four separately styled
item references remain visibly empty pending B54. There is no MP bar.

The inspector formats another subject exclusively from the existing eight-field
observation, with Unknown HP/level/class/build/skills/ranks/wallet/inventory.
Residents and contenders share the same stranger label; explicitly hostile
sentinels retain their public cue. Commands and self resources never switch to
that subject. Selection brackets and range/confirmation visuals use authorized
observations and own definitions. Distant source sight permits inspection, with
Out of reach feedback; it never supplies body reach or automatic travel for a cast.
Sight loss clears selection, outline and targeting, with no automatic reacquisition.

`LDMinimap` draws unexplored dots, explored-unseen hatching and currently visible
terrain. Dynamic markers use the filtered view and disappear on stale/lost sight.
Public service/objective markers, tower offline state, source counts and projected
camera footprint use retained public facts. The existing Presentation byte marks
own versus observed foreign wards; it adds no owner identity or network field.
The eight observed fields and 22 public fields remain unchanged.

LMB capture pans/drags the camera, without moving the body or expanding sight.
The first RMB while an action is armed consumes cancellation. A subsequent RMB
samples only authored static floor/blocker channels and issues a ground order
through the existing native navigation validator. Walls/roofs and wrong floors
reject. Map gestures never select dynamic markers, cast or target a hidden body.
HUD hit regions and map capture suppress world fallthrough; major panels block
map commands, and edge pan stays suppressed during map dragging/arming.

One progress widget presents actual own action timing, holds and pending results.
Incoming cast/damage warnings remain above major panels. Own HP/life and match
time remain visible while panels are open. HUD read models refresh in controller
updates, independently of Slate painting. A bounded 128-result local cache
deduplicates identical nonzero request/result pairs; different requests with the
same message still renew their outcome. It adds no replicated field.

The frontend now says Host private lobby and removes the Migration demo title.
The private class/Ready lobby uses the same panel language. Death permits
read-only build inspection, blocks world commands and shows the actual loss and
return countdown. A new life clears targeting and restores follow/protection.
The terminal panel follows the actual objective receipt, including No acquisition,
and offers return/new-lobby navigation. Another contender's death never awards a win.

The retained four-book development panel still backs P/I/Bag. Inventory, quick
item use, vendor redesign and peer trade are B54–B56; no such behavior is claimed.

## Approved engine baseline

The user explicitly approved adopting installed Unreal **5.8.4 CL 58902052** on
22 October after the version guard found the installation had updated. The
platform lock now names that exact version; the guard remains active. The selected
compiler is MSVC 14.44.35229, Windows SDK 10.0.26100.0 and .NET Framework SDK 4.8.
Builds/probes ran sequentially with MaxParallelActions 4 on the 16 GB machine.
Historical B47–B52 evidence retains its original 5.8.3 provenance, including the
historical baseline in the UI contract JSON.

## Verification

Evidence paths are relative to repository-root `Artifacts/`.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Toolchain preflight | Passed | `20261022-082927-635-Preflight` |
| Final Editor build | Passed | `20261022-085654-730-BuildEditor` |
| Rules, including HUD and reflected privacy schema | 17 passed, zero warnings | `20261022-085210-108-Automation/Report/index.json` |
| Independent Editor HUD/input, target 150 ms / jitter 25 ms | Server 26 / A 36 / B 36 passed before final canonical-actor assertion | `20261022-084950-744-HUD-150-25` |
| Retained movement | 7 checks passed | `20261022-085245-641-MovementCheck` |
| Retained variable-frame combat | 48 checks passed | `20261022-085300-635-CombatCheck-VariableFrames` |
| Retained V04 region | 93 checks passed | `20261022-085318-385-Region-DevelopmentCheck` |
| Native class/Ready lobby, 720p | Captured and visually inspected | `20261022-085433-249-InputUI-1280x720-Lobby/Lobby.png` |
| Native death, 720p | Captured and visually inspected | `20261022-085457-215-InputUI-1280x720-Death/Death.png` |
| Native controls with own HP/life/time above panel, 720p | Captured and visually inspected | `20261022-085543-242-InputUI-1280x720-Controls/Controls.png` |
| Native selected subject, 720p / 125% text | Final wrap/height visually inspected | `20261022-085710-878-InputUI-1280x720-Selected-125pct/Selected.png` |
| Native No acquisition, 720p | Final content-sized result panel visually inspected | `20261022-085736-140-InputUI-1280x720-Result/Result.png` |
| Win64 game target | Passed | `20261022-085800-192-BuildGame` |
| Existing 28 saved assets | Validate passed without writes | `20261022-090103-491-Bootstrap-Validate` |
| Win64 cook/stage/archive | Passed | `20261022-090127-479-PackageWin64` |
| Cooked HUD/input, target 100 ms / jitter 25 ms | Server 26 / A 37 / B 37 passed | `20261022-090230-358-Packaged-HUD-100-25` |
| Cooked HUD/input, target 150 ms / jitter 25 ms | Server 26 / A 37 / B 37 passed | `20261022-090346-537-Packaged-HUD-150-25` |
| Cooked HUD/input, calibration base 46 ms / jitter 25 ms | Server 26 / A 37 / B 37 passed; measured 140.65 / 141.32 ms | `20261022-090839-881-Packaged-HUD-150-25` |
| Cooked HUD/input, final calibrated near 150 ms / jitter 25 ms | Server 26 / A 37 / B 37 passed; measured 153.40 / 153.59 ms | `20261022-091035-845-Packaged-HUD-150-25` |
| Cooked regional navigation regression | Server 34 / A 17 / B 17 passed | `20261022-090502-891-Packaged-Navigation-150-25` |
| Cooked filtered-view regression | Server 12 / A 29 / B 27 passed | `20261022-090645-328-Packaged-ViewNetworkCheck` |
| Cooked own HUD, 720p | Captured and visually inspected | `20261022-090703-313-Packaged-InputUI-1280x720-HUD/HUD.png` |
| Cooked masked selection, 1080p | Captured and visually inspected | `20261022-090713-819-Packaged-InputUI-1920x1080-Selected/Selected.png` |
| Cooked masked selection, 720p / 125% text | Captured and visually inspected | `20261022-090723-108-Packaged-InputUI-1280x720-Selected-125pct/Selected.png` |
| Cooked death, 720p | Captured and visually inspected | `20261022-090732-328-Packaged-InputUI-1280x720-Death/Death.png` |
| Cooked No acquisition, 720p | Captured and visually inspected | `20261022-090741-689-Packaged-InputUI-1280x720-Result/Result.png` |

`RunHUDCheck.ps1 -Region` extends the B52 native input fixture with direct minimap
gestures, owner/foreign source distinction, 200 cm authored plateau, blocked map
cell, panel suppression, receipt duplication, distant public-source inspection,
restricted canonical actor inventory, stale view removal, read-only death, actual
return and terminal No acquisition. This is synthetic input/gesture engineering
evidence, not physical-pointer or human usability acceptance. Positions/builds/HP/
points, one cooldown reset, a 100-gold death balance and a terminal deadline are
explicit fixtures; actual hold, death/return and objective-result rules still run.
The Editor run targeting 150 ms measured median echoes of 170.06 / 169.83 ms;
those actual observations do not establish a measured 150 ms run.
Cooked HUD/input medians were 99.55 / 99.64 ms and 157.96 / 167.91 ms. All
configured delay runs use up to 25 ms one-way jitter; target names are not
substituted for measured transport. The higher-delay cooked run passed its
named behavior checks, without claiming an exact 150 ms measurement.
Calibration at a 46 ms one-way base produced 140.65 / 141.32 ms; the final 49 ms
base produced **153.40 / 153.59 ms**, passing all 26/37/37 checks. Reproduce this
machine's near-150 ms HUD lane with `-RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 49`,
and inspect actual echoes when running on another machine. The 100 ms lane used
a 30 ms base. No source/configuration changed between these timing runs.

The cooked navigation regression measured echoes 168.40 / 156.98 ms, initial
acknowledgement p95 199.54 / 219.24 ms and route acceptance p95 264.78 / 219.24 ms.
Each owner performed 14 native queries (0.85 / 1.04 ms total). The authority-started
Step fixture sampled corrections up to 79.27 / 122.48 cm and the server-only
obstruction fixture 46.01 / 43.62 cm; the respawn route sampled about 1.74 cm.
The earlier B52 fixtures also produced large Step/obstruction corrections. This
is retained native prediction behavior, not correction-free movement or a full
UI/performance acceptance claim. In particular, the route acceptance p95 is
reported separately from the initial acknowledgement metric.

The filtered-view regression checked restricted native registries, distinct body/
ward/public sources, concealment ordering and 250 ms stale removal. Maximum
serialized views were 876 / 775 bytes in that fixture; this is not a full-game
traffic budget measurement. The reflected rule test separately enforces the
exact eight observed and 22 public fields.

Native captures record actual viewport, effective UMG scale and text preference.
The captured viewport scale is 0.666 at 1280×720 and 1.0 at 1920×1080; the larger
text capture records a 1.25 preference. No Windows display-scale setting was changed
or inferred from those UMG values.
The 720p default band remains 180 physical pixels (270 logical at 2/3 scale).
The 1080p band uses 270 pixels instead of the proposed 220: inspection found the
smaller proposal clipped multiline Unknown fields and bottom commands. The 125%
text setting gets 330 logical pixels. B57 still owns the complete presentation,
focus and declared OS-scale matrix; these dimensions are recorded implementation
choices, not changes to gameplay or the frozen specification.

Final archive: `Artifacts/20261022-090127-479-PackageWin64/Archive/Windows`.
Its 121 source/configuration and seven payload hashes match the workspace with
zero mismatches in `Artifacts/B53PackageProvenance.json`. The manifest names B52
`9f4c58f` plus the dirty B53 working changes committed with this report; this
does not claim the earlier clean B52 revision contained B53. Source/configuration
stayed fixed through cooking and all cooked probes. No authored/frozen input/map
asset was changed. This was a local warm build/cook, not a fresh checkout.

## Failures found and corrected

The first 5.8.4 Editor compile found an unused invalid camera include; it was
removed. The first rendered capture asserted when adding a TArray element to its
own storage; the camera footprint now closes from a copied point. Further images
caught auto-wrapped hotbar columns and insufficient 1080p band height; captions
now have bounded lines and the band has enough room.

The first HUD network extension failed its headless widget-text assertions because
HUD refresh depended on painting. Refresh now runs in the controller update.
Two fixture assumptions were corrected against retained rules: death of a
100-gold contender loses 15 gold, and map travel is measured from each body's
actual starting position. Failed evidence remains at `20261022-084040-185-HUD-150-25`;
failed compile/captures remain under their original timestamped artifacts. A
receipt-direct death fixture can emit native saved-move warnings while the old
body is unpossessed; it is not a measured ordinary-combat death/performance pass.

## Review boundary

Full v0.5 U01–U56 acceptance is open. B54–B56 inventory/vendor/exchange, B57 final
scaling/focus and B58 cold checkout, restart, normal peaceful UI journey,
performance and uncoached human review remain. Local builds/cooks are warm.
Dedicated source-engine packaging and remote Git/LFS delivery remain conditional
or unverified. Do not resume the user-deferred 90-minute stress run or claim
full 60–90 minute pacing, OS-scale acceptance or human participants from fixtures.
