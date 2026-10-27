# B90 — full-map river contact and bridge clearance

27 October 2026 · Native source **daa7d9e**. Continues the [spatial foundation](B90_IMPLEMENTATION.md) and
[native traversal increment](B90_TRAVERSAL.md).

## Behavior and scope

The standalone full-map mode now samples the floor immediately beneath the
native character capsule. Grounded contact with an authored river or the western
Heart water inset applies Wet immediately and refreshes it on 100 ms boundaries.
Leaving water retains Wet for three seconds. A frozen floor applies 30% movement
slow through CharacterMovement's maximum speed: 420 becomes 294 cm/s. Surface
contact adds no Stun and never removes the river's underlying walkable floor.

The contact reducer is shared with the existing v0.8 combat director. Its existing
Wading, Ice Grip, protection, NPC immunity, extinguishing and timestamp behavior
remain in that reducer. The new spatial preview has no equipment or paid spell
dispatch; it uses ordinary contact defaults. Its HUD reports its own Wet,
frozen-floor contact and surface slow.

Full-map cells use a separate sparse 1,200 × 1,200 address space with 200 cm cells
and origin (−120,000, −120,000). This does not resize or reinterpret the legacy
60 × 40 fog/region wire format. The floor sample also checks height: a frozen
river cell cannot slow a character walking on the dry deck above it.

Frozen patches in the native acceptance fixture are explicitly diagnostic seeds,
guarded by `-LDB90RiverProbe`. They call the existing environment Water/Frost
transition, have ten-second lifetimes, and enforce 64 owner / 1,024 map cell
bounds. There is no normal input, client RPC or free inventory cast attached to
this fixture. Paid Frost, action ordering, ownership/life authority, effects on
other contenders and restricted observation remain B91 integration work. These
fixtures do not certify B94 field-capacity stress.

## Fitter revision 2

The original three low bridges had 200 cm deck tops and 40 cm slabs, leaving
only 160 cm below for a capsule 180 cm tall. Their deck tops are now 300 cm,
leaving 260 cm below; the eastern bridge remains at 400 cm. Broad bank aprons
remain part of the fit.

Authored layout revision 1, seed 9001, the incoming 59-file package and source POI
coordinates stay unchanged. Fitter revision 2 explicitly identifies this native
geometry change. `NavigationTarget` selects the fitted dry layer for bridge POIs;
road fitting, native pad evidence and walk fixtures use the same target. Source
and fitted coordinates remain separate in the reports. The compiled payload
SHA256 is `80edef1004242f5044a5aa916949ba7960eecebb6a8d83a0a4165b950832255c`.

## Acceptance method

`RunB90River.ps1` starts the saved full map at fixed 1/60 s native ticks. Each
case begins with a labelled fixture placement, then walks through ordinary
navigation and CharacterMovement. The 55 cases cover:

- Four dry bridge decks over frozen river cells, four thawed underpasses and
  four frozen underpasses.
- Three named fords, each thawed and frozen.
- Every one of the 17 route/river intersections, each thawed and frozen on the
  river floor. These include all 14 authored wet contacts and the floor beneath
  the three contacts classified as bridge routes; deck behavior is checked
  separately above.
- The Heart water inset thawed and frozen, plus its dry claim circle.

The probe records every movement tick, floor contact, Wet, frozen contact, slow,
velocity and maximum speed. It compares native navigation paths before and after
freezing, checks no Stun/falling, measures steady ice velocity, and waits after
actual water exits to check residual Wet and expiry. An independent Python
checker derives expected case identities from the compiled map, verifies every
trace interval and movement bound, and reconciles sample counters. It also
matches every derived intersection against the supplied route contact list to
within 2 cm, independently of the native fixture. Dry seed
rejection and cell-address/reducer boundary rules are checked separately.

Reproduction, after syncing and building Editor:

~~~powershell
./Scripts/BuildB90Map.ps1
./Scripts/RunB90River.ps1
./Scripts/RunB90River.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/RunB82Check.ps1
./Scripts/RunB90Traversal.ps1 -Lane All
./Scripts/BuildGame.ps1 -MaxParallelActions 3
~~~

Run native operations sequentially with Source/Config fixed. Historical B90 and
traversal evidence describes fitter revision 1 and remains immutable. The new
evidence must identify its own source and fitted assets.

The separate rendered lane walks only the frozen Heart inset. Its cyan outline
marks a diagnostic patch; it is not final ice art. The captured native HUD shows
Wet active, frozen-floor contact, 30% surface slow and a 294 cm/s maximum speed.
The complete 55-case run uses NullRHI and is retained separately from this image.

## Results

| Check | Result |
| --- | --- |
| River contact journeys | 55/55 pass; 21,564 native movement samples |
| Native water exits and residual expiry | 42/42 pass |
| Steady frozen-floor speed | 294 cm/s; ordinary Wet has no surface slow |
| Frozen navigation / control | No path changes, Stun or falling |
| Rendered Heart inset | Separate native walk and HUD capture pass |
| Reopened map | 2,559 complete paths; 31,909 ground samples; zero missing ground |
| Maximum sampled road grade | 24.0617% |
| Independent generation / reopen | Both fitter revision 2 generations and reopen have identical canonical pads and actor/instance transforms |
| Fitter revision 2 traversal replay | 31 journeys and 15 boundary cases pass; zero falling/replans |
| Traversal duration | 6,150.234 aggregate simulated walking seconds; 185.101 wall seconds |
| Antechamber → Heart | 10.683 s in the full traversal replay |
| Longest 50 m POI-anchor gap | 339.067 s; existing functional-density risk remains open |
| Native rules | 50 pass, including contact and cell-boundary rules |
| Editor / Game targets | Win64 Development builds pass on pinned Unreal 5.8.4 |
| v0.8 B82 network regression | 56 server / 23 A / 23 B checks pass at configured 100 ms RTT, ±10 ms half-lag |

The one conservative capsule contact at the eastern bridge entry remains the
same as fitter revision 1 and is corroborated by the actual native entry walk.
Both independent fitter revision 2 generations and reopen have identical
canonical pads and actor/instance transforms. After comparison, the exact first
generation map/definition and unchanged terrain/material bytes used by the
movement tests were retained; all 148 asset hashes match the audit.

Raw reports, traces, image, logs and hashes are retained in
[the portable evidence bundle](evidence/B90River/README.md). The ledger records
this as contact-fixture acceptance. Full B90 release acceptance remains partial
where sight, services, reset accounting and functional-density work overlap
later stages. No full-map competitive session or human pacing acceptance is
inferred from these focused tests.
