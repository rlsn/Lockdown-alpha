# B52 input and action modes

21 October 2026. Runtime predecessor: `607e2d8` (B51). Native Enhanced Input,
UMG/Slate, CharacterMovement and the retained authority are still the integration
points. This is an input checkpoint; B53–B58 remain required for full v0.5.

## Implemented behavior

`ULDControlSettings` owns the 26 command definitions, defaults, displayed key
labels, conflict checks and local SaveGame preferences. The controls panel
supports keyboard/mouse capture, reserved Escape, reset, edge pan, initial follow,
saved zoom and a text-scale preference. Capture runs before child button handling.
Repeated remaps remove their previous Enhanced Input delegates and contexts.
First launch checks whether the save exists before attempting a read. Failed saves
produce session-only feedback. The isolated persistence test never writes the
player's normal preferences slot.

The V05 controller removes the generated legacy context and the old native skill
context, then installs this registry. Q/W/E/R arm equipped skills; 1–4 are reserved
for the future carried-item references and currently report empty. F is context;
Space recenters the camera. WASD movement, global Ambush/Step, F ward, contextual
E, H help and immediate left-click attack are absent from the normal mapping.
The frontend Host button and implicit regional launch now select V05. Explicit
`Profile=V04` preserves historical regressions; the unqualified legacy sandbox
retains its historical profile.

A arms one directional basic attack. Targeted skills arm without spending a
cooldown or cancelling travel. Confirmation uses only the current owner-filtered
observation and explicit ground direction. Spark/Ambush and targeted held tasks
resolve that exact observation on authority, then use their existing legality and
impact rules. A missing or forged handle cannot fall back to a nearby body. The
old direct skill and hover-facing RPCs cannot bypass V05 slots or turn idle facing.
The ordinary basic impact still chooses its retained nearest legal cone target.

Patch uses its initiating key/button; targeted channels use the confirmation
button; contextual channels use F. Release ends that gesture, with the existing
100 ms refresh and 250 ms stale expiry. Accepted stationary channels stop
navigation. S stops locomotion and interruptible holds while preserving committed
combat and cooldowns. Own state is applied from action receipts before Pending
aim clears, preserving the committed facing while navigation continues.

Inspection stores only an audience-scoped observed handle and generation. Losing
observation clears inspection, armed targeting and a held target. Selection never
changes possession or rotates the owned body. The camera and approach cursor use
the filtered view, without querying canonical remote actors. Camera movement
does not expand sight or action reach.

Major development/help/settings panels stop navigation, release holds and block
world commands across the viewport. Closing requires every held physical input to
release; Windows foreground input also checks physical key state when Slate has
consumed a release. Focus flush requires the same barrier. Apply validates and
closes the preview, then arms a fresh F station hold; Use book follows the same
pattern. A temporary development-panel button arms retained Seal fabrication.

## Verification

Evidence paths are relative to repository-root `Artifacts/`.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Editor build | Passed | `20261021-162410-942-BuildEditor` |
| Rule suite including real save/load | 16 passed, zero test warnings | `20261021-162601-223-Automation/Report/index.json` |
| Independent V05 input clients, 150 ms target RTT / 25 ms jitter | Server 15 / A 19 / B 19 passed | `20261021-162623-795-Input-150-25/{Server,A,B}.json` |
| Retained variable-frame combat | 48 checks passed | `20261021-162751-065-CombatCheck-VariableFrames` |
| Retained V04 regional gameplay | 93 checks passed | `20261021-162809-206-Region-DevelopmentCheck` |
| Win64 game target | Passed | `20261021-162823-616-BuildGame` |
| Existing 28 saved assets | Validate passed without writes | `20261021-163015-474-Bootstrap-Validate` |
| Win64 cook/stage/archive | Passed | `20261021-163038-208-PackageWin64` |
| Cooked input at measured ~100 ms | Server 15 / A 19 / B 19 passed | `20261021-163244-343-Packaged-Input-100-25` |
| Cooked input at measured ~150 ms | Server 15 / A 19 / B 19 passed | `20261021-163327-309-Packaged-Input-150-25` |
| Cooked regional navigation regression | Server 34 / A 17 / B 17 passed | `20261021-163408-931-Packaged-Navigation-150-25` |
| Native 1280×720 controls panel | Captured and visually inspected | `20261021-163551-788-Packaged-InputUI-1280x720-Controls/Controls.png` |
| Native 1280×720 temporary HUD | Captured and visually inspected; B53 layout pending | `20261021-163601-640-Packaged-InputUI-1280x720-HUD/Region.png` |

Final archive: `Artifacts/20261021-163038-208-PackageWin64/Archive/Windows`.
Its 113 source/configuration hashes and seven payload hashes match the workspace
with zero mismatches in `Artifacts/B52PackageProvenance.json`. It was built from
`607e2d8` plus the B52 working changes committed with this report. No authored,
frozen generated input or map asset was changed. This is a warm local build/cook,
not the B58 cold-checkout gate or remote Git/LFS delivery.

Cooked input echo medians are 99.62 / 99.70 ms and 149.70 / 149.71 ms. All these
delay runs configure 25 ms one-way jitter. The packaged navigation run measures
149.52 / 149.76 ms, initial acknowledgement p95 183.04 / 183.07 ms and route
acceptance p95 232.68 / 183.07 ms. Both owners perform 14 native queries. Ordinary
travel corrections remain roughly 1.74–1.98 cm in those phases. The retained
authority-started Step case has sampled corrections of 142.10 / 105.33 cm, and
the intentional server-only blocker about 38.05 cm. These are recorded native
prediction limitations, not correction-free movement. See B51 for that fixture
and adapter; this checkpoint does not add a second movement integrator.

Rendered captures use the existing regional Visual fixture and local preferences.
The settings scroll area, live keys and all footer controls fit at 720p; long
button labels wrap. The HUD still uses the old layout with updated bindings.
These captures do not establish the OS/text-scale matrix or human usability.

The input clients measured echo medians 149.59 / 149.62 ms. They inject native
key events into independent controllers. Explicit observation/ray fixtures supply
the confirmation point; this does not establish physical pointer usability.
Checks cover remapping without delegate growth, retired Q/numeric aliases, armed
cancellation, forged observation rejection, exact Spark selection over a nearer
target, early and completed Patch holds, sight-loss invalidation, global panel
suppression, fresh release after close, real station preparation, camera ownership
and A-confirm melee followed by S without a cancelled or duplicated strike.

Fixture positions, NPCs, equipped knowledge, HP and XP are injected explicitly.
One cooldown is reset between the early-release and completed Patch cases. Actual
hold durations, authority, native key routing, cooldown charging and packet delay
remain real. No fixture proves earned progression, human review or the full UI.

## Review and remaining work

The first test export contained 15 ordinary successes plus the successful new
mapping test with a missing-save warning. The sixteen-test runner rejected that
incomplete ordinary-success count. Checking save existence removed the first-run
warning; the final export has sixteen warning-free passes. Earlier failed builds
(test-local name shadowing and a private RPC access in the probe) remain separate
artifacts and are not passing evidence.

The eight-field observed mask and 22-field public allowlist are unchanged from
B51. The new action receipt contains only the owning player's state and facing.
No opponent stats, wallet, bag, class or canonical actor references were added.

The existing HUD is temporary. B53 still owns anchored commands, inspector,
targeting/range visuals and minimap gestures. B54 owns the real 24-slot container,
quick references and drag capture; B55/B56 own quoted vendor and peer exchange.
I currently opens the retained book/development panel; T reports unavailable.
The saved text-scale preference is applied to controls rows; B57 must apply and
verify it throughout the final presentation and complete keyboard alternatives.
Thus U02–U04 and U14 are not claimed as complete release-wide acceptance.

Cold checkout, the current normal 25-minute journey, ten restarts, full scaling
matrix and actual uncoached human review remain for B58. The user-deferred
90-minute stress run is still deferred. Retain B47/B51 unaffected evidence and
their explicitly recorded limitations.

## Reproduce

```powershell
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/RunTests.ps1
./Scripts/RunInputCheck.ps1 -Region -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 54
./Scripts/CaptureInputUI.ps1 -Controls
./Scripts/OpenStandalone.ps1
```

`RunInputCheck.ps1` and `CaptureInputUI.ps1` also accept `-PackageRoot`. The
network runner owns and cleans up its three processes. Capture uses real rendered
native widgets offscreen; inspect the resulting PNG rather than treating file
creation as a layout pass. Run Unreal builds and probes sequentially with source
stable on the audited 16 GB machine.
