# Acceptance and delivery evidence

## Planned checks

These checks define the future build's acceptance. They are not claims that an Unreal project has already been generated, compiled or played. Use native automation for pure helpers and functional or process-level scenarios for world, network and packaged behavior. Epic provides an Automation Test Framework; select the APIs and runner supported by the pinned engine. [Epic testing reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine).

| ID | Check and expected evidence |
| --- | --- |
| U01 Baseline and environment | Actual v0.2 revision and toolchain are recorded, or missing inputs are identified; no inference from old documents is reported as an observed build |
| U02 Cold build and lock | Editor target builds from the pinned version without Live Coding residue; local engine path is not hard-coded into committed files |
| U03 Module boundaries | Runtime builds without editor dependencies; editor generator and tests are correctly isolated |
| U04 Valid assets | Bootstrap saves real maps and assets, validate rerun is idempotent and invalid references fail with useful diagnostics |
| U05 Cooking | Required maps, soft references, materials, input and widgets are present in a cooked package; no missing-content fallback hides errors |
| U06 Native entry | Host, join address, failure message and leave work through UI without the console |
| U07 Conversion and movement | Heart maps to origin, 2400 by 1600 becomes 4800 by 3200 cm, squared ranges scale by four, normalized movement matches 180 and 300 cm per second |
| U08 Ramp and dash | Walkable ramp works, cliff cannot be stepped up directly, dash covers at most 360 cm in 200 ms and cannot tunnel through tree, wall or cliff |
| U09 Input and camera | Cursor aim uses valid ground, menu clicks do not attack, Escape restores focus, focus loss releases holds and wider camera view grants no sight |
| U10 Presentation | Human-sized shared humanoid, facing, locomotion, attacks, warnings, prompts and fog are readable at 720p and 1080p; capsule-only debug graphics do not satisfy final review |
| U11 Basic attack | Legal 20-damage hits retain 150 ms windup and 750 ms start cadence within the declared sampling tolerance; misses consume cadence and no unintended cleave occurs |
| U12 Assassination | Rear and quiet checks, 60 damage, 500 ms warning, recovery and cooldown are enforced; turning, moving out, dashing or damaging the attacker interrupts when appropriate |
| U13 Authority | Cross-owner, stale match or life, out-of-range, invisible-target and fabricated-damage requests fail on the server |
| U14 Simultaneous events | Valid lethal impacts in one authority batch can kill both; a dead body cannot complete another transaction in that batch |
| U15 Respawn arithmetic | Reference delays are 20 seconds for level one at zero, 40 for level two at 240, and 105 for level three at 480; a phase change does not extend an existing timer |
| U16 Retained and private state | Gold loss applies once, 101 gold at level three loses 31, retained state persists and only the owner sees exact HP, balances and contract |
| U17 Life replacement | Return uses a new body ID and full upgraded HP; four-second protection blocks actions and contest; old-life requests cannot buy, collect or claim |
| U18 Connection lifecycle | Leave, disconnect and return behavior are explicit; stale holds stop within 250 ms plus a measured sample allowance; cleanup creates no automatic winner |
| U19 Objective-only victory | One living contender or zero living bodies does not award a win; a deadline without a claim returns No claim |
| U20 Real courier loop | Guild acceptance, three-second collect and delivery, interruption, private stage and one 25-gold 15-XP receipt run through authority; duplicate completion cannot pay twice |
| U21 Seal ownership | A fixture seed creates one item; real pickup, death drop, corpse independence and simultaneous recovery preserve one owner, with no stale-life duplication |
| U22 Access policy | Ordinary claim is locked until all four defensive flags are cleared; one Seal carrier can bypass while the flags remain; no Seal-less bypass |
| U23 Claim contract | Ten-second uncontested hold completes once; movement, damage including the completion batch, release, stale hold, death, range loss or another unprotected contender in 240 cm resets progress |
| U24 Height | Low sight cannot see raised targets or across raised terrain; elevated sources see lower terrain; ramp transitions update the body's vision level |
| U25 Opaque geometry | Trees and walls block both source heights; camera occlusion fade does not reveal entities or grant sight |
| U26 Source union | A private remote ward helps its owner only, public tower sight helps both clients, and panning adds no source |
| U27 Observation payload | Restricted client payloads contain no hidden transforms, role, exact enemy HP, inventory, job, owner, canonical pointer or GUID-linked identity |
| U28 Unreal backdoors | Client actor registry, PlayerState, GameState, attached actors and RPCs expose no enemy canonical body or identity-to-proxy mapping |
| U29 Concealment | The first outgoing revision after source loss omits hidden positions; receipt removes proxies and effects, with no moving last-known marker |
| U30 Loss and reordering | Lost full views recover, old views cannot recreate a concealed actor, delayed revocations cannot remove a later valid reappearance and stale-view timeout hides remote proxies |
| U31 Secondary cues | Hidden shadows, sounds, trails, selection, decals and hit text do not reveal unauthorized positions; private assassination warning remains readable |
| U32 Waiting view | Death removes body sight while retaining only legitimate ward and public sources; a free camera cannot reveal more |
| U33 Ward implementation | One initial charge, 560 cm sight, 60 second expiry, 20 HP, legal 96 cm placement, cap two and authoritative destruction all work without claiming a shop port |
| U34 Crowd | Eight server residents follow valid paths and stay nonviolent; role-neutral proxies and materials do not identify humans automatically |
| U35 Fixture boundary | Diagnostic seeds and defensive toggles require the scaffold profile and authority permission, are logged and are labelled Fixture only in the ledger |
| U36 Packaged network | Independent packaged game processes host and join; two remote clients also pass the private-view tests against a separate server world |
| U37 Performance | Declared hardware, settings and topology support measured render, authority and view budgets; no full-population capacity claim is inferred from the small slice |
| U38 Delay | At measured 100 and 150 ms RTT, movement correction, warning, interruption, receipts and proxy removal remain consistent; report timing and visual limitations |
| U39 Restart and isolation | Ten restarts clear views, NPCs, life schedules, receipts, items, exploration and fixture state; old generations cannot mutate a new run |
| U40 Honest handoff | Fresh-checkout setup, version lock, logs, asset manifest, migration ledger, actual results and unported systems are delivered; conditional dedicated validation is not labelled passed without evidence |

## Test method and tolerances

Pure helper tests verify exact unit conversion, integer currency loss, cooldown and respawn formulas, route access and single-result predicates. World tests verify real collision, ramp geometry, source inclusion, AI pathing, actual possession changes and native widgets. Network tests must use independent client worlds and include the real request and publication paths.

Unit-clock attack tests reproduce the baseline ideal impact times of 0.15, 0.90, 1.65, 2.40 and 3.15 seconds for five ordinary hits. Native runtime tests compare these deadlines with the documented authority sampling allowance, rather than asserting identical CMC trajectories or cross-engine bitwise replay. A substantial timing change is a deviation requiring investigation.

Inspect real restricted-client state, not only server output. A server debug overlay containing hidden state is expected to know the world and proves nothing about client privacy. Tests should enumerate client actor types and serialized view fields and exercise static map, dynamic actor and cosmetic leakage paths.

Use two remote clients against a server world for fog tests. A host and one remote client are useful for entry-flow tests, but insufficient to prove symmetric privacy. Test process logs must identify listen host, editor server, remote client and dedicated server roles correctly.

## Initial measurement targets

Measure on the declared development machine, with a cooked Development game where practical. These are proposed scaffold targets, not benchmark results.

| Measurement | Initial target and conditions |
| --- | --- |
| Rendering | Median at least 55 FPS and 95th percentile frame time below 33 ms at 1080p in a 60 second active scene |
| Authority rule and view work | 95th percentile project rule and view work below 10 ms per 50 ms authority interval; measure separately from total engine frame time |
| Relay bandwidth | Mean below 100 KiB per second per client during a ten minute slice session; report other native movement and RPC traffic separately |
| Action acknowledgement | 95 percent within 250 ms at measured 150 ms RTT, excluding no failed requests silently |
| Stability | Ten minute network run plus ten restarts without sustained memory, source, timer or actor growth after cleanup |

Record CPU, GPU, RAM, driver, engine patch, resolution, scalability, shader-warmup state, frame cap, server topology, count of NPCs and client processes. If multiple rendered clients share one GPU, report that condition. Do not call its frame rate an isolated client benchmark.

Shape native game traffic in both directions using an available engine-supported or network-level method, and verify the actual RTT. HTTP browser throttling is irrelevant to the native transport. Include up to 25 ms one-way jitter. Test visibility loss and reappearance under dropped or reordered observation messages as a separate transport case. Do not equate message deletion with realistic TCP behavior from the old prototype.

## Required delivery evidence

Deliver project source and essential assets, a pinned configuration, working scripts, an asset manifest, a runnable Win64 Development package or an exact documented build blocker, and the completed ledger and report. A blocker does not become a pass; required scaffold acceptance remains incomplete until the missing engine build, asset generation or run has happened.

Use Passed, Failed, Not run or Conditional not available for each check, with command or scenario, actual result and evidence path. The dedicated packaged server lane is conditional, while cold editor build, ordinary Windows package and separate remote-client checks are required. A new Server target file alone is not dedicated server evidence.

Provide a short demo guide and recording or screenshots covering frontend, 3D movement, ramp and tree sight, private ward, shared sensor loss, warning and return, courier receipt, Seal theft and final claim. List every diagnostic setup action. Do not imply that a recording of the slice establishes full profession, tower-combat, economy, AI contender or complete-match parity.

## Review before the next port stage

Ask reviewers whether movement, aim, depth, threat warnings and interaction feedback are clearer than in the web prototype. Investigate repeated confusion before expanding the port. If human reviewers are unavailable, finish the engineering evidence and label this usability review pending.

Advance toward full v0.2 parity only when the scaffold builds reproducibly, required authority and visibility tests pass, the native UI is usable and remaining gameplay is accurately inventoried. Then port the next highest-risk subsystem from the ledger rather than adding unrelated features.
