# Working port slice and build plan

## The required demonstration

The final scaffold is labelled Unreal migration demo. A reviewer hosts or joins through native UI, controls a readable humanoid in a small crowd, moves up a real ramp, observes cover and private versus public sight, fights another contender or a diagnostic dummy, sees a death and return, completes a courier delivery and performs a valid Heart claim. A carried Seal can be dropped and stolen through real transactions.

Two independent remote clients must exercise the same authority paths. The host may configure diagnostic tower flags or seed the test Seal through explicit fixture controls. Those setup actions are not gameplay parity. They must appear in the report and ledger as fixtures.

## Slice behavior

| Area | Required native behavior |
| --- | --- |
| Frontend | Host demo, join direct address, show connection failure and leave cleanly |
| Crowd | Eight nonviolent server NPCs wander validated routes with shared humanoid presentation; no hidden role labels |
| Controls | Walk, sprint, aim, attack, dash, assassination, contextual hold, ward and help |
| Combat | Retain converted reach and v0.2 timing; authoritative HP, warnings, miss and rejection feedback, cancellation and simultaneous impacts |
| Life | Death receipt, fixed return deadline, retained state, new body ID and four second protection |
| Courier | Guild acceptance, one district destination, three second collect and three second deliver, exactly 25 gold and 15 XP once |
| Ward | One initial charge, legal placement, private source, expiry and destruction; no shop required |
| Public sensors | At least one active tower source affecting both clients, with authority-controlled loss and recovery |
| Access policy | Four defensive flags, ordinary unlock only when all false, or a carried Seal bypass |
| Seal | One explicitly fixture-seeded dropped item, real pickup, carry, drop and contested recovery |
| Heart | Real ten second hold, 120 cm interaction, 240 cm contest, interruption, shared claim sensor and one winning receipt |
| Reset | Restart the demo cleanly, clearing sources, NPCs, items, channels and old life requests |

Use the frozen v0.2 gameplay for retained combat and death arithmetic, with the converted spatial values. The one courier route can repeat after its retained 20 second cooldown, but no full profession-rank and crafting claim is made. XP can update the baseline hero level through the existing thresholds; upgrade offer selection remains deferred unless already cheaply ported and verified. Record this partial progression explicitly.

For a self-contained combat baseline, the basic attack chooses the nearest other visible legal target in an 80 degree total cone at impact, dealing 20 with 150 ms windup and a 750 ms start cadence. A miss consumes cadence. A living contender has 100 HP and a resident 40. There is no passive regen, armour, random critical hit or damage scaling in this slice.

Assassination selects the nearest eligible target within a 60 degree total aim cone and 112 cm. The attacker must lie within 60 degrees of the target's rear, be unexposed and not sprinting or dashing, and have spent one continuous second at walking speed or slower without an attack or dash. The target must have neither dealt nor taken damage in the previous four seconds. Lock the target on acceptance, consume an 18 second cooldown and give the target an immediate 500 ms warning. Recheck rear, distance, sight, target damage history and both alive at impact. Any earlier damage to the attacker cancels the windup. Do not recheck the attacker's newly applied exposure as a failure. A hit deals 60 and has 250 ms recovery; a scheduled failed impact also recovers, while an earlier damage or dash interruption ends the action without refunding cooldown. An exactly overlapping target has no rear bearing and is ineligible.

Attacks, dashes and taking damage apply four-second observed exposure; damaging a body in an active public tower's sight applies 12 seconds to the attacker. Expiry only extends. These cues still require authorized sight. Killing a resident grants nothing. Full contender kill payouts, assists and anti-feeding economy remain Deferred; do not introduce a template kill-score winner. The courier receipt is the slice's required paid XP and gold path.

Residents return after 20 seconds with full HP and a fresh observed body identity, replenishing the small crowd. Corpses last 20 seconds and are separate from dropped items. The contender return uses the retained formula and fixed death-time snapshot, not the resident timer. Connected-session death is supported; authenticated reconnect across disconnection is explicitly deferred as described in the scaffold contract.

The demo has no automatic winner from killing everyone. A claimant needs actual access, even if a fixture supplied it. A timer can terminate the demo as No claim, preserving the 720 second reference cap, but must never award a score or survivor winner.

## B30 Inspect and pin

Audit the actual web prototype and Unreal environment. Capture a reproducible web recording and its tuning where available. Fill initial ledger rows, engine lock and asset policy. Choose the available build lane. Keep the prior prototype intact.

Done when setup and source comparison status are explicit, a particular engine is selected, and no old browser assumption is silently treated as native behavior. Missing source or toolchain is recorded precisely, not converted into a fake test pass.

## B31 Create the valid C++ project

Create runtime and editor modules, targets, configuration, build scripts, tests and source-control rules. Cold-build the Editor target. Implement the editor-only asset generator and save the frontend, sandbox, tuning, input and UI assets.

Done when the project opens without missing-class or missing-asset errors, bootstrap validation is idempotent and runtime code has no editor-only dependency. A handwritten `.uproject` without a successful build is not completion evidence.

Depends on B30. Primary checks U01–U05.

## B32 Establish the 2.5D feel

Implement unit conversion, the small 3D level, ramps, collision channels, Character Movement, camera, cursor aiming, sprint and native HUD foundation. Add a coherent shared humanoid placeholder and eight wandering residents.

Done when ordinary movement, screen-relative controls, aiming, ramp traversal and body scale are readable in the editor and a standalone run. Dash and combat can follow in B34, but the movement pipeline must already be native and authoritative.

Depends on B31. Primary checks U07–U10 and U34.

## B33 Prove client ownership and filtered observation

Implement host and join, server contender records, owner state, canonical body isolation, the world-view relay and neutral remote proxies. Add source union, explored fog, trees, high-ground policy, private ward and shared tower sensor. Include view revisions, concealment and recovery from dropped or reordered views.

Done when two remote clients have different legitimate information, no hidden canonical body or owner mapping reaches either restricted client, source loss removes fresh hidden updates and camera movement adds no sight. Do not add additional gameplay before this test is credible.

Depends on B32. Primary checks U06, U13 and U24–U33.

## B34 Port combat and the life loop

Implement the single basic attack, compatible dash, warned assassination, damage batches, death, loss, return and protection. Preserve turn, range, terrain and damage interruption counterplay. Wire visible effects through the same observation boundaries.

Done when two human-controlled clients can fight and evade through authority, a death does not end the demo, and a return cannot replay old commands. Check timings under delay before broadening content.

Depends on B33. Primary checks U11–U19 and U31.

## B35 Add the representative interactions

Add the real courier transaction, real Seal ownership lifecycle, the access predicate and the ten second Heart claim. Add clearly scoped authority-only fixture controls for acquisition and defensive flags. Implement native prompts and progress for all required interactions.

Done when a reviewer can complete the courier loop with an acknowledged reward, steal a dropped Seal and win through a legally uninterrupted claim. Also demonstrate ordinary access using fixture-cleared defences. Do not describe those fixture setups as full tower or profession ports.

Depends on B34. Primary checks U20–U23 and U35.

## B36 Package and verify

Cook and package a Win64 Development game. Run host and join from packaged processes, then the separate-server privacy topology. Run automation and functional scenarios, network-delay and performance measurements, reset and cleanup tests, and the readability review at both supported resolutions.

Done when setup from a clean checkout and the required package work, results are backed by logs, and the ledger separates ported, replaced, fixture and deferred behavior. A dedicated-server target that cannot be built on the available toolchain is labelled conditional and unverified.

Depends on B35. Primary checks U36–U40 and the complete required suite.

## B37 Review before a full parity release

Compare the Unreal demo against the web recording for scale, timing, input responsiveness and clarity. Resolve presentation or authority problems before migrating all v0.2 systems. Produce the next-port order from ledger risk: full tower and camp progression, full profession and crafting, economy and upgrade offers, autonomous contenders, target population and complete-match parity.

This is a review and continuation plan, not permission to add the entire backlog to v0.3. Keep the original gameplay vision and source package available for the next step.

## Working cadence

Keep a runnable checkpoint after each stage. Compile after small reflected C++ changes, run the relevant tests, and inspect the actual scene after layout or UI changes. Preserve a continuation note with last completed stage, actual failure and next task if an external dependency prevents completion. Do not ask for approval on routine reversible implementation choices already within the task.
