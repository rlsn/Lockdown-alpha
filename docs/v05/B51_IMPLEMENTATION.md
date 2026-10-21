# B51 — Native navigation foundation

21 October 2026. The navigation adapter is implemented and its independent-client
Editor and cooked gates pass, together with the affected retained regressions.
This is the foundation checkpoint; B52 input migration and B53–B58 remain ahead.

## Implemented behavior

`ULDNavigationComponent` validates destinations on authority and follows a local
static native route on the owning client, feeding the existing CharacterMovement
input. The server performs its own complete-path query, intended-surface and
projection checks. It sends only the accepted endpoint/tolerance and order result,
never a complete route or hidden actor reference. Authored static navigation is
enabled on clients. Server collision, acceleration, speed enforcement and root
motion remain native; there is no second position integrator or AI possession.

Match/life/order/epoch metadata travels in native saved moves through a custom
`FCharacterNetworkMoveDataContainer`. Moves cannot combine across identities.
Authority rejects acceleration from an inactive, cancelled or old order. Stopping
horizontal movement preserves gravity and committed root motion. Replayed route
acknowledgements cannot restart a cancelled or old-life route.

Requests coalesce to the newest order at ten route evaluations per second. An
approach checks at most twelve bounded ring candidates. Navigation heartbeats are
500 ms with a two-second authority expiry; a heartbeat cannot revive an ended
order. Each side monitors monotonic along-path progress: after two seconds without
10 cm advancement it replans once, then stops after a further two seconds. Initial
pending acknowledgement and route acceptance have separate latency measurements.

Observed-unit approaches use one authorized snapshot and cancel on sight loss.
They never chase, attack, loot or channel. Public stations use static positions.
Unit/station stopping fits within 100 cm; pickup stopping fits within 64 cm,
including endpoint tolerance. Final steering slows through native analog input
to reach tight endpoints without bypassing native acceleration/braking.

Camera supports bounded arrow/edge pan, the retained fixed rotation and zoom,
Space temporary recenter and Home persistent follow. Pointer input over current
HUD/panels is excluded from move/edge-pan handling. Full gesture ownership,
rebinding and minimap input belong to the following input/HUD milestones.

## Profile and review boundary

`?Profile=V05` enables the new adapter while retaining the v0.4 development rules.
Its navigation context is installed only in V05, so S does not steal the retained
V04 movement key. WASD steering and cursor-facing polling are disabled in V05.
Space invokes camera recenter there. Existing skill/context/development entries
remain checkpoint controls until B52 replaces them coherently.

The ordinary frontend still launches V04 at this checkpoint. For an explicit
interactive native navigation lobby use:

```powershell
pwsh -NoProfile -File Scripts/OpenStandalone.ps1 -NavigationCheckpoint
```

Choose class, Ready and Start through the real lobby. RMB moves/approaches, S
stops, Shift sprints, arrows/edges pan, Space recentres, Home toggles follow and
the wheel zooms. Digits still invoke the retained skills; E/P remain the old
context/development workflow. This is not the final v0.5 control mapping or HUD.

## Executed Editor evidence

Final runtime Editor build:
`Artifacts/20261021-145117-274-BuildEditor/BuildEditor.log`.
The final prerequisite repair builds are indexed in the delivery table below.

The final regional independent-client run passes **Server 34 / A 17 / B 17**:
`Artifacts/20261021-145305-019-Navigation-150-25/{Server,A,B}.json`.

```powershell
pwsh -NoProfile -File Scripts/RunNavigationCheck.ps1 -Region `
  -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 54
```

Measured client echo medians are **149.60 / 149.55 ms**. Initial move acknowledgement
p95 is **166.46 / 183.01 ms**; accepted-route p95 is **166.46 / 232.68 ms**. These
are measured native transport results, not WAN or human response measurements.
The one-way injection base and full command are saved in `Environment.json`.

Cases include both clients routing around authored obstacles, sprint, replacement,
Stop/stale acceptance, 200 cm ramps, wrong-floor rejection, forged observed
handles, moving-target snapshots, sight loss, pickup approach, heartbeat expiry,
a 100-order burst per client, focus flush, Step, a late collision blocker, falling
after Stop, ordinary return timers and fresh navigation after respawn. No other
canonical bodies appear in a restricted client.

Pickup distances were **62.94 / 62.95 cm**, with both books still dropped. Authority
made 33 planning calls across the run, including rejected projections and bounded
approach candidates. Owning clients made 14 / 13 calls, costing 0.743 / 0.719 ms
in total. This is headless path-query evidence, not a rendered UI performance pass.

Corrections are explicitly recorded by phase. Ordinary obstacle/ramp/sprint
samples outside initial fixture settling were about 1.74–1.98 cm; Stop/sight-loss
phases reached 16.62 / 30.60 cm. Retained authority-originated Step produced sampled
corrections up to 110.43 / 98.91 cm. The intentionally server-only late blocker
produced repeated corrections up to 35.00 / 44.16 cm before bounded stop. Fixture
placement corrections are separately named and must not be called normal travel.
No claim of correction-free or universally smooth movement is made.

## Cooked delivery and retained regressions

| Case | Result | Artifact directory under Artifacts/ |
| --- | --- | --- |
| Rules and privacy allowlists | 15 passed | `20261021-145853-401-Automation` |
| Retained native movement | 7 passed | `20261021-145914-571-MovementCheck` |
| Retained variable-frame combat/life | 48 passed, open dash 360.18 cm | `20261021-145928-972-CombatCheck-VariableFrames` |
| Retained V04 region | 93 passed | `20261021-145946-197-Region-DevelopmentCheck` |
| Final Editor / Game builds | Passed | `20261021-151957-341-BuildEditor`; `20261021-152011-744-BuildGame` |
| Final Win64 package | Passed, 28 generated packages validated | `20261021-152127-712-PackageWin64` |
| Cooked regional navigation at measured ~150 ms | Server 34 / A 17 / B 17 passed | `20261021-151425-083-Packaged-Navigation-150-25` |
| Final cooked sandbox navigation at measured ~100 ms | Server 34 / A 17 / B 17 passed | `20261021-152458-853-Packaged-Navigation-100-25` |
| Native 720p checkpoint HUD capture | Captured and visually inspected; existing HUD, not B53 delivery | `20261021-152813-171-B51-Native-UI-720/Region.png` |

The regional cooked run used the preceding archive
`20261021-150145-061-PackageWin64/Archive/Windows`. The final sandbox repair only
executes on that legacy map when valid bounds are absent; the regional path and
its saved envelope are unchanged, so that unaffected result is retained.

The final archive is
`Artifacts/20261021-152127-712-PackageWin64/Archive/Windows`. Its 103 source/config
hashes and seven payload hashes match current checkpoint files, with zero
mismatches in `Artifacts/B51PackageProvenance.json`. Packaging began from
`c222021` plus the B51 working changes committed with this report. No hand-authored
or frozen generated map/input asset was modified.

Cooked regional clients measured echo medians 149.64 / 149.66 ms, initial
acknowledgement p95 182.97 / 183.11 ms and route acceptance p95 182.95 / 183.11 ms.
The final sandbox clients measured 99.58 / 99.68 ms, initial acknowledgement p95
133.09 / 133.07 ms and route acceptance p95 199.37 / 133.07 ms. Both runners retain
25 ms configured one-way jitter. These headless listen-authority runs are not a
packaged dedicated server, human session or rendered performance measurement.

## Corrections and evidence limits

Earlier runs remain under Artifacts. The first sandbox run exposed two probe
defects: an inverted camera-toggle assertion and a final server read after clients
disconnected. The first pickup fixture was mistakenly left in the book class's
default owned state, so it was correctly absent from observation; it is now
explicitly dropped. These failed runs are not passing checkpoint evidence.

The first cooked sandbox run (`20261021-150409-259-Packaged-Navigation-100-25`)
failed because its historical BSP-only navigation volume had no cooked bounds.
The normal region already uses `ALDRegionNavBounds`. The sandbox now creates the
same noncolliding 4800×3200×800 envelope, on authority and clients, only when no
valid authored bounds remain. Valid Editor bounds and all saved assets stay
untouched. The final cooked sandbox run above proves the repair; the failed
archive/run remain distinct evidence.

Review also corrected horizontal cancellation resetting native falling velocity,
tight endpoint tolerance extending past pickup range, and the new S mapping being
installed in the old profile. The added probes cover falling and pickup endpoints.

The retained privacy schema check caught the new public `bUIControlProfile` flag.
Its exact allowlist now names that global profile fact and expects 22 public
fields. The eight-field observed-unit allowlist is unchanged; no opponent stats,
items, wallet or canonical references were added. This is an explicit schema
addition, not removal of privacy validation.

Native fixtures set initial body positions, create observed targets/books and a
late non-navigation collision box, grant Step for its isolated case and terminate
life through the real life function. Travel, collision, packet transport, channel
cooldown, clocks and return deadlines remain real. No fixture establishes normal
economy, uncoached usability or full release acceptance. Existing 90-minute stress
deferral and human-review gaps remain unchanged.
