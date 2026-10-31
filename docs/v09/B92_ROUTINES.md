# B92 — native civilian and traveler routines

31 October 2026 · B92 remains **in progress**

Native source **a556ae738f7bb0ccd1a6c6db9bef4ec2d67f1d59**. [Frozen evidence](evidence/B92Routines/README.md)
extends the [earned progression checkpoint](B92_CHARTER.md).

## Behavior and authority

The 96 civilian and 32 traveler source IDs now own nonreplicated native AI
controllers on their existing bodies. They do not create contender seats, item
sources, wallets or rewards. Native path-following and CharacterMovement perform
all walking. Requested goals project to nearby native navigation. Failed
requests wait five seconds before retrying; interrupted paths replan. There is no schedule teleport or distant
body replacement. Root/Stun and other movement limits apply to the full-map NPC
movement path; surface and ordinary slows remain real.

Civilian offsets select their initial activity and remaining desired dwell from
the supplied 600-second work/market/rest/return-to-work cycle. Each later phase
keeps its supplied duration. Travel happens before the dwell clock and can make
the whole loop longer than 600 seconds; overdue phases do not warp or catch up.
Work and market use accessible public service fronts, rest uses its supplied
public POI, and home bodies begin on fitted exterior pads. Coincident public
activity points may change activity without an unnecessary invented walk. Interior
home entry, door ownership routines and convincing activity animations remain
separate review work. Standing at work does not produce a free economic receipt.

Travelers begin at the authored origin, with a staggered part of the initial
45-second dwell remaining. They walk the public shortest strategic route, using
its ordered polyline points as native path goals, dwell 45 seconds on arrival and
return along the graph. Travel does not spend a courier reward budget. A blocked
waypoint cannot be skipped by teleporting to the next settlement.

The sealed pack is observed cosmetic geometry. The authorized body view adds
only visible velocity and a sealed-pack flag, with no role enum, source identity,
wallet, destination or inventory/market stock. Leaving sight retires the body
proxy and its pack together. The pack has no collision, pickup, Take command,
item identity or economic output. It must not imply an interceptable caravan.

## Danger, interruption and source continuity

Routines sample actual visible public attack/hit/cast cues and the body's own
received impact. The normal full-map range, geometry, smoke, ledge, concealment
and Blind boundaries apply. Looking up an authority body to test a candidate does
not grant its hidden position as a goal: only a passed sight test can refresh
observed danger. A distant attack cannot command a flee order.

A frightened body heads toward a known public guard/post, keeps its activity,
remaining dwell and route, and resumes after ten seconds without observed danger.
It does not attack, join an army or create a crime incident. Local guard response
and lawful/unlawful action classification remain the next security layer.

A defeated role retains its source ID and existing account. Its controller and
routine retire with the body; no replacement wallet, items, XP or gold are granted.
The native roster audit now distinguishes a defeated source from a missing live
body. This increment does not implement guard returns or source repopulation.

## Validation and limits

The short smoke suite passes21 connected checks. The full suite passes23
checks across900.095seconds of game time and900.163seconds of wall time, using
89 timestamped snapshots. All128 bodies moved more than10m before the labelled
death; every surviving civilian (95) completed at least four phase transitions.
All32 travelers completed a strategic leg,17 completed at least a return, and
54 legs finished in total. Final routine state is94 dwelling/33 traveling/one
defeated. No native move/goal requests failed; the snapshots contain no stationary
travel interval. Recorded maximum planar speed is approximately420cm/s, with a500cm/s
teleport/overspeed detector bound.

The path-request diagnostic records1,058 MoveTo requests,60.49ms total request
time and0.163ms maximum. It excludes goal projection, sensing, physics and
engine frame work, so it does not establish full query or frame-time targets.
Only eight actual sight rays were needed by the controlled danger cases. The
remote capture shows the moving pack outside HUD occlusion; hidden proxies
retire in the actual client view.

Independent replay closes **169 receipts,149 accounts,579 historical items
(564 live),3,263gold**:3,288start −25 funded institution burn, with **zero mint**.
Five ordinary institution ticks create ten goods and consume fifteen existing
market units. Routine activities, packs, Root, fleeing and the labelled death
create no item source or progression. Original finite world goods remain intact.

Rules59/Charter62/courier23/services-work83/economy125/roster37 pass.
B91 source/control53 and combat50 plus both full-unity targets pass. All207 protected generated assets/supplied inputs are unchanged.

The initial compile diagnostics exposed header-order, name-shadowing and test-only
access mistakes. The first smoke run passed Root, perception and remote pack
checks but failed its observer-placement assumption: every suitable starter pad
was inside a traveler neighborhood. The final fixture searches checked public
wilderness pads instead. An intermediate run was intentionally stopped while
cue timing was tightened to the observer's current sample; a visible idle body
cannot inherit an earlier hidden attack. The first independent full-run check
assumed90 exact snapshots; frame-relative sampling produced89 spanning the full
observation. Validation now checks timestamp coverage/gaps instead of that
incorrect exact-count assumption. Final captures keep the observed pack
clear of the HUD, and trip counters count each departure only once. Shared parent
visibility could expose the added pack on ordinary avatars; a separate hidden-in-
game gate now keeps it hidden unless the observed sealed-pack cue enables it.
The actual client also checks that its ordinary owner avatar does not render it.
A later smoke attempt exposed movement between positive sight setup and attack;
the final fixture briefly Roots that subject, rechecks current sight and allows
up to three ordinary attack attempts without changing their cadence/windup.

The short smoke run and final observation use the same compiled runtime. The
final acceptance requires fifteen real minutes without time dilation or schedule
fast-forward. Only the remote observer is staged for visibility checks. Root,
ordinary attacks and one noncontender death are labelled authority fixtures;
NPC locations and development are never granted. Institution ticks continue on
the normal match clock and their creation/consumption/burn receipts remain
separate from cosmetic activity. Full ledger replay checks that separation.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Routines.ps1 -Capture -Seconds 900
./Scripts/RunTests.ps1
./Scripts/RunB92Charter.ps1 -Capture
./Scripts/RunB92Courier.ps1 -Capture
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunB92Economy.ps1
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/RunB91Combat.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather -MaxParallelActions 4
python -B Scripts/ExportB92RoutinesEvidence.py docs/v09/evidence/B92Routines --verify
```

Guard/security and ambient hostile behavior, observation-limited contender AI,
simulation tiers, durable recovery, broad race/stress and normal human pacing
remain open. All routine sensing currently uses a uniform planning cadence;
path-query totals and costs are diagnostics, not near/mid/far performance
acceptance. Native logs contain repeated late authority-sample warnings; the
server30Hz/frame-time target is not established by this observation. Visual life and a 15-minute engineering observation do not establish
a complete living-world match. `v09_full` remains closed; B93/B94 are separate.
