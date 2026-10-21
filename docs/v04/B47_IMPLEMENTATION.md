# B47 — Release engineering and review

21 October 2026. Core implementation and completed checks are recorded. At the
user's direction, the stress run was stopped and full stress validation deferred
to the next version. No critical issue remains in the completed checks. This
record distinguishes ordinary practice, fixtures and outstanding validation.

## Implemented changes

Authority and sight sampling previously scheduled the next interval from the
actual late frame. At the cooked game's frame cadence, that reduced the intended
20 Hz rule/sight sampling to about 16 Hz and delayed view publication. Sampling
now retains its scheduled phase across ordinary frame quantization, while a
large stall discards old intervals. Each tick still evaluates the current pose
once; no historical catch-up batches are replayed. The timing rules cover a
62 FPS simulation, stall resynchronization and retained deadline boundaries.

The bottom HUD panel now measures its wrapped content at a fixed readable width
and anchors its actual height above the screen edge. The previous fixed height
could push the hotbar out of view when the contextual prompt wrapped.

Variable-frame network verification also exposed a retained dash overshoot:
387.02 cm against the intended 360 cm. Unreal's constant-force source disables
partial final ticks by default, adding a complete last movement tick. The dash
now enables that partial tick while retaining native swept movement, replicated
root motion, zero finish velocity and the 200 ms duration. The variable-frame
48-check combat probe passes at 360.18 cm (the engine's small endpoint epsilon),
including wall/ramp/ledge/tree collision; native and cooked remote owners also
reconcile at 360.18 cm. No distance tolerance was enlarged.

Build scripts accept an optional MaxParallelActions limit. The default remains
Unreal's choice; bounded builds reduce memory pressure on the declared machine.
The test runners now support a real ordinary 25-minute regional session and a
separate technical soak up to 5400 measured seconds.

## Test profiles

The ordinary session chooses the actual Rogue start, earns Witness, traverses
the three visit triggers, demonstrates autonomous NPC Patch after a nonlethal
hit, completes eight paid jobs and fabricates from those earnings. It then
continues native town/Guild travel until the original deadline. The intended
terminal result is no acquisition, retaining the unconsumed earned Seal. No
resource/build grants, teleports, clock changes or deadline extension are used.
The shorter B46 ordinary run remains the earned winning-route demonstration.

The technical soak uses two independent remote owners on the regional map and
a headless listen authority with a nonparticipating local connection. Initial
and returned bodies are placed in the sustained-load scene. Above 1400 seconds,
the harness extends only the match deadline to keep gameplay active through the
measurement. World time, economy, progression and ordinary return delays remain
unchanged. Optional LifeCycles fixtures periodically place two eligible bodies,
set target HP to one, clear the attacker's action/cadence and resolve an immediate
lethal combat batch. These force the actual corpse/death/return cleanup path;
they do not establish ordinary combat balance or counterplay. Fixture CPU cost
is recorded separately from regular authority sampling.

Minute audits record actors, process memory, world time, deadline, result serial,
life/death state, receipts and Theft draw/wallet counters. Remote audits reject
canonical opponents, private directors and physical books/Seals. The current or
retiring owned body is allowed during asynchronous life teardown. Frame, GPU,
acknowledgement and traffic metrics distinguish rendered and headless clients.
Rendered checks validate the actual 1920 × 1080 viewport and require GPU samples;
an offscreen-window fallback cannot pass the 1080p test.

Default rendering measurements retain current project settings, including Epic
quality and ray tracing. Automatic screen percentage is recorded as configured;
1920 × 1080 refers to the output viewport. The optional High profile is explicit
and is not used to conceal a Default-profile failure. GPU driver budget/usage,
texture memory and Windows per-process GPU counters are retained where available.

## Verified build and functional evidence

Current archive: `Artifacts/20261021-092725-233-PackageWin64/Archive/Windows`.
The Development Win64 manifest records 99 source/configuration files and seven
payload hashes. Runtime checkpoint: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
Packaging occurred from `7088e6d` with B47 working changes; all 99 current
source/configuration files and seven payloads match the manifest's SHA-256
records. Final edits affect documentation only. A fresh B47 cold checkout was
not run during wrap-up; B46's prior cold-checkout result remains historical
evidence, not a cold build of this revision.
The toolchain is UE 5.8.3 CL 58210709, MSVC 14.44.35229 and Windows SDK
10.0.26100.0. The catalog hash is
`6b0e3a6ac94830536e29afc428f0b865dbba7db1b7e1167e4823db4ce6954e5c`.

Every directory below is under the ignored repository-root Artifacts/.
Native means the installed Editor executable; cooked means the archive above.
The retained objective/movement checks and early life-cycle smoke precede the
dash correction; final variable-frame/native/cooked combat and the final archive
checks explicitly cover that correction.

| Case | Actual result | Artifact directory |
| --- | --- | --- |
| Final Editor build / native rules | Passed / 15 passed | `20261021-092324-526-BuildEditor`; `20261021-092400-978-Automation` |
| Final variable-frame combat | 48 passed; open dash 360.18 cm; wall, ramp, ledge and tree collision | `20261021-092428-900-CombatCheck-VariableFrames` |
| Final native delayed skills / combat | Server/A/B 8/5/6 and 9/16/19 passed | `20261021-092446-280-Skills-DevelopmentNetwork-150-25`; `20261021-092517-524-CombatNetwork-150-25` |
| Final Game build / asset validation / cook | Passed / 28 packages / Win64 archive produced | `20261021-092613-313-BuildGame`; `20261021-092704-047-Bootstrap-Validate`; `20261021-092725-233-PackageWin64` |
| Cooked life/withdrawal/roster audit | 8 passed; level-five death at 1200 s loses 16 of 101 gold and waits 46 s; waiting withdrawal charges once and cannot return | `20261021-093219-128-Packaged-ReleaseAudit-Region-DevelopmentCheck` |
| Cooked main / alternate region | 93 / 93 passed | `20261021-093222-770-Packaged-Region-DevelopmentCheck`; `20261021-093226-378-Packaged-Region-DevelopmentCheck-Alternate` |
| Cooked fabrication / preparation / theft / skills | 27 / 61 / 56 / 62 passed | `20261021-093230-010-Packaged-Seal-Region-DevelopmentCheck`; `20261021-093233-649-Packaged-Preparation-DevelopmentCheck`; `20261021-093236-255-Packaged-WorldChecks-DevelopmentCheck`; `20261021-093238-872-Packaged-Skills-DevelopmentCheck` |
| Cooked ten regional resets | 95 passed, including dirty craft history and physical-item cleanup | `20261021-093241-494-Packaged-Seal-Region-DevelopmentReset` |
| Native delayed theft at 100 / 150 settings | Server/A/B 7/6/6 each | `20261021-091227-222-WorldChecks-DevelopmentNetwork-100-25`; `20261021-091305-617-WorldChecks-DevelopmentNetwork-150-25` |
| Cooked delayed theft / preparation / skills | Server/A/B 7/6/6, 9/6/6 and 8/5/6 passed | `20261021-093252-290-Packaged-WorldChecks-DevelopmentNetwork-150-25`; `20261021-093316-911-Packaged-Preparation-DevelopmentNetwork-150-25`; `20261021-093341-517-Packaged-Skills-DevelopmentNetwork-150-25` |
| Cooked delayed earned Seals | Server/A/B 21/6/7 passed | `20261021-093401-027-Packaged-Seal-Region-DevelopmentNetwork-150-25` |
| Cooked combat / restricted views | Server/A/B 9/16/19 and 12/29/27 passed; dash 360.18 cm | `20261021-093135-161-Packaged-CombatNetwork-150-25`; `20261021-093522-852-Packaged-ViewNetworkCheck` |
| Cooked 720p fabrication HUD | One fixture check passed; capture inspected, full hotbar visible | `20261021-093540-285-Packaged-Seal-Region-DevelopmentUI-720` |
| Retained objective scenarios | Main 53, exact deadline 3, late claim 3, no claim 4, withdrawal 5 passed | `20261021-085754-918-ObjectiveCheck` |
| Retained movement | 7 passed, including native collision/navigation | `20261021-090336-164-MovementCheck` |
| Early native three-minute life-cycle smoke | Zero failures on Server/A/B; one explicit lethal batch, actual ordinary return | `20261021-085919-555-Editor-V04-Soak-180-150-25-Default` |

Development transaction/network cases deliberately use grants, positions and/or
controlled deadlines; the original earned B46 practice and the ordinary B47
session remain separate. Native delayed theft uses the original injection
settings and records actual echo values in its client reports. The calibrated
measurements below establish the near-100/150 ms performance evidence.

## Measured cooked rendering and transport

Machine: Ryzen 9 7945HX, 16 GB RAM, 8 GB RTX 4060 Laptop GPU, driver 32.0.16.1742.
Measurements share the user's normal machine workload. Each profile has two
independent automated remote owners, eight residents and a headless listen
authority with a nonparticipating local connection. It is not a dedicated target,
WAN test, four-human capacity measurement or human multiplayer session.

Each short profile measures 60 seconds after ten seconds of warmup in the Guild
load scene. A renders
an actual 1920 x 1080 offscreen viewport; B and authority use NullRHI. The project
retains default Epic settings and ray tracing. The only Default-profile console
command is t.MaxFPS 60 on rendered A; headless roles request 120 and the authority
requests NetServerMaxTickRate 120. Actual client frame cadence and echo RTT are
recorded, rather than inferred from those settings. Automatic screen percentage
remains enabled: 1080p is output resolution, not fixed 100% internal resolution.

| Target RTT / one-way base / jitter | Actual echo median A/B ms | Action acknowledgement p95 A/B ms | A active median FPS / active frame p95 ms | GPU p95 ms | Authority + view p95 ms |
| --- | --- | --- | --- | --- | --- |
| 100 / 25 / +/-25 ms | 91.43 / 93.77 | 166.05 / 157.99 | 60.00 / 29.96 | 7.64 | 0.57 |
| 150 / 50 / +/-25 ms | 153.24 / 149.71 | 202.93 / 200.42 | 60.01 / 30.05 | 7.67 | 0.60 |

Evidence: `20261021-093015-123-Packaged-V04-Soak-60-100-25-Default` and
`20261021-092852-499-Packaged-V04-Soak-60-150-25-Default`.
Both authorities retained 1201 measured samples, approximately 20 Hz. Both overall
and active-body graphics measurements pass median 55 FPS and frame p95 below
33 ms. The unchanged action acknowledgement target is p95 at most 250 ms.

| Target | Native inbound A/B KiB/s | Serialized full-view A/B KiB/s | Driver local budget / usage / over-budget MiB on A |
| --- | --- | --- | --- |
| 100 | 10.35 / 10.56 | 8.30 / 8.51 | 7188 / 2161.13 / 0 |
| 150 | 10.18 / 10.66 | 8.36 / 8.54 | 7188 / 2162.05 / 0 |

Both traffic measurements pass 100 KiB/s. Per-process Windows GPU counters,
engine texture/driver statistics, actual viewport, quality values and PNGs remain
in the artifacts. Final 1080p and 720p captures were inspected; the hotbar fits.
No GPU-budget warning appears in these captures. Historical B45 warnings remain
recorded: these short passes establish neither a GPU-memory root cause nor
90-minute rendered durability. The long technical soak is headless.

## Failed intermediate evidence

- `20261021-083540-802-Editor-V04-Soak-60-150-25-Default` used an actual
  888 x 500 viewport. It is excluded from 1080p evidence. The runner now forces
  and checks the actual viewport and requires GPU timing samples.
- `20261021-084634-156-Packaged-V04-Soak-60-150-25-Default` passed graphics
  but failed action acknowledgement p95 at 250.44 ms against 250 ms. Investigation
  found authority/view cadence drift; the scheduled-phase correction was then
  tested without relaxing that threshold.
- `20261021-091607-317-Packaged-CombatNetwork-150-25` used the original
  75 ms one-way injection, yielding actual echo medians 213.46/213.89 ms. A's
  six scripted action acknowledgements had p95 263.64 ms; B measured 244.61 ms.
  This whole run is failed evidence even though server semantics passed. The
  runner now accepts an explicit one-way calibration and records Transport.json;
  original defaults and acknowledgement thresholds remain unchanged.
- `20261021-092103-011-Packaged-CombatNetwork-150-25` passed acknowledgement
  timing but exposed the 387.02 cm dash overshoot. Enabling partial final ticks
  corrected the actual runtime; distance tolerances remain unchanged.

## Ordinary 25-minute result

`20261021-093549-000-Packaged-Session25m-Region-Normal` passed all 37 checks in
1500.028 wall-clock seconds. It used the original clock/deadline/rewards and
native movement with no grants, teleports or build overrides. Starting Survey
earned Witness; three visits and eight actual deliveries reached 420 XP/level
five; a nonlethal hit triggered the trained NPC's autonomous Patch. Four-second
fabrication spent the actual earnings. Native travel totalled 395830 cm.

The original deadline committed one no-acquisition result and retained one
unconsumed earned Seal, the private craft receipt, eight delivery receipts,
100 gold and four unused credits. There were zero deaths, peaceful NPC kills
or Theft draws. This route deliberately did not claim Heart; B46 retains the
ordinary earned winning-route evidence.

Twenty-six audits recorded 92–99 actors and 326.42–374.46 MiB process memory.
Late-half memory spread was 47.42 MiB, below the unchanged 128 MiB target.
Final rule/view p95 values were 0.049/0.304 ms, with their sum below 10 ms.
This was one automated headless practice contender, not human multiplayer or a
rendered 25-minute performance test.

## Technical stress run deferred by user

`20261021-100052-183-Packaged-V04-Soak-5400-150-25-Default` was stopped at
10:11:25 local time on 21 October 2026 at the user's request to wrap up and move
to the next version. The last complete authority audit measured 600.1 seconds;
five forced lethal batches passed, in addition to an ordinary combat death.
Actual ordinary returns and restricted-client privacy audits passed during
the observed interval. The final audit showed authority/A/B actor counts
102/58/65 and process memory 352.0/349.1/347.4 MiB. No LD_SOAK failure was logged.

The three owned processes were verified against the archive path and stopped.
`InterruptedByUser.json`, role logs, Environment.json, LifeFixture.json and
process samples preserve the partial evidence and overrides. Stopping before
Finish intentionally produced no final role reports; the runner's missing
Server.json error records cancellation, not an independently observed gameplay
failure. No final acknowledgement/frame/memory percentile result or 90-minute
pass is claimed. The full 5400-second stress case remains deferred.

The headless harness extended the deadline from 1503.901 to 5523.909 world
seconds, retained real clocks and ordinary return rules, and used the declared
initial/returned-body placement and lethal-combat fixtures. This partial run
does not establish ordinary pacing, long rendered durability or RNG stress.

## Delivery and next-version handoff

The documentation/provenance audit checks 72 Markdown files, all local links,
39 feature rows, 48 acceptance rows, four frozen predecessor snapshots and the
package hashes: `Artifacts/B47DocumentationAndProvenance.json`. Specifications,
blank templates and predecessor references remain unchanged. The working ledger
and acceptance table preserve partial/deferred rows rather than treating the
user's scope decision as a test pass.

Carry forward full stress validation, a fresh cold B47 checkout and actual human
review. Dedicated packaging, remote Git/LFS delivery, authenticated resume and
full-session human pacing retain their existing conditional/deferred boundary.
No next-version feature scope is inferred from closing this checkpoint.

## Release privacy audit

The source review follows each disclosure path, alongside the native schema
tests and independent restricted-client scenarios:

| Path | Reviewed boundary |
| --- | --- |
| Canonical records | GameMode owns contender/build state; rule, development, region and wallet directors do not replicate to observers. |
| Owning connection | PlayerController carries only its own OwnerState, including class, attributes, points, library, ranks, slots, cooldowns, wallet, Witness and craft receipt. Character and PlayerState are relevant only to their owner. |
| Filtered observations | The custom serializer permits eight entity fields: observer-specific ID, position, facing, speed, presentation, kind, action cue and exposure endpoint. No build, wallet, difficulty, book skill or craft history is included. |
| Public match state | The 21-field allowlist contains shared match, lobby, defence, attempt/result and sensor state. It contains no contender build or wallet. |
| Widgets and effects | Private HUD/preparation reads the local owner's state. Other bodies use neutral presentation and filtered action/exposure cues; proxies receive no opponent library or skill ranks. Public service thresholds are intentionally visible. |
| Loss of observation | Conceal/full-view generations and revisions reject stale restoration; the 250 ms stale-view timeout removes proxies and current sight. |

An observable movement speed, attack, heal or successful public action can still
inform a player's inference. This audit establishes the stated data boundary;
human judgments about how obvious a build feels from play remain pending.

Human review remains separate. Use the [review guide](B47_REVIEW_GUIDE.md) for
the specified sessions and record actual observations. Automated native clients
are not independently controlled humans. Dedicated packaging, remote Git/LFS
delivery, authenticated resume and 60–90 minute human pacing remain conditional,
deferred or unverified as specified.
