# B36 verification - 19 October 2026

The Windows Development package and the engineering checks below are implemented.
The ten-minute packaged soak passed; its measurements are recorded below.
Human usability acceptance and a source-engine dedicated Server target remain open.
Artifact names retain the host's 18 October timestamps.

| Check | Result | Evidence under Artifacts/ |
| --- | --- | --- |
| Final build/cook/stage/archive | Passed; Win64 Development, both targets; file-backed cook, IoStore/pak | 20261019-001732-992-PackageWin64/Package.log; PackageManifest.json |
| Packaged native UI | Host720p, independent Join1080p, Return to menu passed | 20261018-173817-126-Packaged-Interactive/Game.log; 20261018-173900-822-Packaged-Interactive/Game.log; B36-UI/ |
| Two packaged restricted clients | Server12 / A29 / B27 passed | 20261018-174507-903-Packaged-ViewNetworkCheck/{Server,A,B}.json |
| Packaged objectives,100ms configured RTT +/-25ms jitter | Assault15/21/18; Seal15/21/18 passed | 20261018-174507-908-Packaged-ObjectiveNetwork-100-25/{Assault,Seal}/{Server,A,B}.json |
| Packaged combat,150ms configured RTT +/-25ms jitter | Server9 / A16 / B19 passed | 20261018-174840-706-Packaged-CombatNetwork-150-25/{Server,A,B}.json |
| Ten world restarts in packaged process | 84 passed;11 clean-world observations | 20261018-175648-214-Packaged-ResetCheck/Reset.json |
| Native combat/movement regression |48 /7 passed | 20261018-175648-230-CombatCheck/Combat.json; 20261018-175648-231-MovementCheck/Movement.json |
| Native rules/schema |8 passed | 20261018-173818-197-Automation/Report/index.json |
| Packaged Seal,150ms configured RTT +/-25ms jitter, rendered720p A | Server15 / A19 / B20 passed | 20261018-175648-207-Packaged-ObjectiveNetwork-150-25/Seal/{Server,A,B}.json |
| Ten-minute packaged soak | All three processes passed; no concurrent build/test | 20261018-175841-491-Packaged-Soak-600-150-25/{Environment,Server,A,B}.json |
| Final-source objective regression | Main53 / ExactDeadline3 / LateClaim3 / NoClaim4 / Withdraw5 passed, including finished-room PreLogin rejection | 20261019-002233-409-ObjectiveCheck/*.json |
| Final-package reset regression |84 passed; ten reloads | 20261019-002233-386-Packaged-ResetCheck/Reset.json |
| Final-package Seal regression,150ms +/-25ms, rendered720p A | Server15 / A19 / B20 passed | 20261019-002233-388-Packaged-ObjectiveNetwork-150-25/Seal/{Server,A,B}.json |

The measured package was 20261018-175244-066-PackageWin64. On Ryzen9 7945HX,
RTX4060 Laptop (driver32.0.16.1742),15.69GiB RAM, DX12 SM6 High at100% screen
percentage, client A rendered1920x1080 with a60FPS cap. B and authority used
NullRHI at120FPS. After ten seconds warmup, the next60 seconds supplied3600
rendered frame samples; gameplay/network activity continued600 seconds with eight
residents, movement, attacks, rejected contextual requests, deaths and returns.

| Measurement | Authority | Rendered A | NullRHI B |
| --- | --- | --- | --- |
| Frame median / p95 | — |60.00FPS /16.83ms | Not a rendering measurement |
| Rule / view / combined p95 |0.050 /0.835 /0.864ms (11,999 samples) | — | — |
| Action receipts / p95 | — |893 /249.60ms |896 /241.13ms |
| Echo median / p95 | — |199.54 /249.73ms |182.98 /224.64ms |
| Snapshot age p95 | — |65.35ms |18.95ms |
| Full-view serialized payload | — |7.06KiB/s |6.07KiB/s |
| All native inbound / outbound | — |8.65 /5.46KiB/s |7.88 /5.74KiB/s |
| Autonomous corrections / maximum | — |6 /35.99cm |0 /0cm |
| Actual body path, excluding pawn replacement jumps | — |103,736cm |100,285cm |
| Actor bounds |68–69 |37–38 |34–38 |
| Late-half memory spread |1.83MiB |6.02MiB |0.13MiB |

Every measured action received an acknowledgement, including rejections. A's
249.60ms p95 passes250ms with a narrow margin; this is one small-slice run on
emulated local transport, not a WAN or capacity guarantee. Snapshot age uses the
replicated GameState clock, not synchronized one-way latency. Total native inbound
traffic includes relay, other replication and framing. Combined authority work
adds vision work to the most recent combat/objective sample; it is not the entire
engine frame. Normal GC was used throughout this soak.

Subsequent source changes fix dead-at-result text, preserve the waiting camera
while suppressing finished input, reject new connections to finished rooms, and
make diagnostic readiness timeouts independent of measurement duration. They do
not change the measured active rules/render path. The final package and regression
evidence are recorded below; the600-second numbers belong to the package above.

The network authority is a separate headless packaged **listen** world with a
nonparticipating local connection and two remote packaged clients. It is not a
dedicated Server target. Uncooked runs still support the editor-server topology.
The first Game -server attempt did not enter dedicated mode and timed out; those
logs are excluded from passing evidence. Native Host/Join and leave were also
operated through the actual packaged UMG buttons at720p/1080p.

The reset lane alternates real pending returns and terminal claims, with dirty
work, resources, receipts, item ownership, sources, exploration and fixture state.
It checks default populations/state, old actor destruction and rejection of old
commands, owner receipts and views after every travel. GC is requested every ten
frames in this cleanup lane; the soak uses normal GC. The listed packaged run's
last-six working-set samples ranged337.65-341.25MiB. This is ten world reloads in one
GameInstance, not ten complete OS process boots or a remote-rejoin test.

An initial cook passed, but Zen staging could not read the local store after three
launch attempts. File-backed cooked output resolved staging while preserving
IoStore/pak runtime delivery. Normal runtime needs no editor or cook server.
One exploratory editor soak ran alongside a build and exceeded250ms p95 on one
client; it is not certified performance evidence. The clean measurement lane
uses one rendered client and no concurrent build/test workload.

## Historical B35 verification

Representative objectives pass the automated slice checks below. This remains an
uncooked migration demo; B36 packaging, complete reset/isolation, human review and
broader performance acceptance are still open.

| Check | Result | Evidence under Artifacts/ |
| --- | --- | --- |
| Editor build | Passed | 20261018-171343-461-BuildEditor/BuildEditor.log |
| Separate Game target | Passed | 20261018-171705-266-BuildGame/BuildGame.log |
| Native rule/schema cases | 8 passed | 20261018-170846-068-Automation/Report/index.json |
| Objective world scenarios | Main 52 / ExactDeadline 3 / LateClaim 3 / NoClaim 4 / Withdraw 5 passed | 20261018-171148-370-ObjectiveCheck/*.json |
| Movement regression | 7 passed; all eight residents moved | 20261018-170955-033-MovementCheck/Movement.json |
| Combat world regression | 48 passed | 20261018-170955-043-CombatCheck/Combat.json |
| Restricted-view regression | Server 12 / A 29 / B 27 passed; max payload 789 / 688 bytes | 20261018-171148-370-ViewNetworkCheck/{Server,A,B}.json |
| Objective Assault, configured 100 ms RTT +/-25 ms jitter | Server 15 / A 21 / B 18 passed | 20261018-171410-577-ObjectiveNetwork-100-25/Assault/{Server,A,B}.json |
| Objective Seal, configured 100 ms RTT +/-25 ms jitter | Server 15 / A 19 / B 20 passed | 20261018-171410-577-ObjectiveNetwork-100-25/Seal/{Server,A,B}.json |
| Objective Seal, configured 150 ms RTT +/-25 ms jitter, rendered 1080p B | Server 15 / A 21 / B 18 passed | 20261018-171410-593-ObjectiveNetwork-150-25/Seal/{Server,A,B}.json |
| Combat network regression, configured 150 ms RTT +/-25 ms jitter | Server 9 / A 16 / B 19 passed | 20261018-171549-933-CombatNetwork-150-25/{Server,A,B}.json |
| Bootstrap Validate | Passed; all 23 package hashes unchanged | 20261018-171834-501-Bootstrap-Validate/Bootstrap.log; B35-AssetHashes.json |
| Editor reopen / MCP play smoke test | Sandbox loaded, All Saved; 67 toolsets; StartPIE/StopPIE passed | B35-MCP/; docs/MCP_TEST_REPORT.md |

The world cases exercise real actors with an injected authority clock: exact work
timings/rewards, replay, cooldown/repetition/level two, movement/release/stale-hold
resets, all fifteen closed defence masks, Seal access, protection/stale life,
completion-batch damage before delivery/claim, corpse-independent inventory, logout
cleanup, scheduled deadline boundaries and one terminal receipt. They do not
replace natural-time network checks or a human playtest.

The objective network lanes use a separate authority and two owning clients.
Actual RPCs collect/deliver, race for one observed item, kill its carrier, steal the
drop, wait for natural corpse expiry and return/protection, contest and finish a
ten-second claim. They audit restricted actor registries, private contract/reward/
inventory and pointer-free public aggregate facts. Diagnostic setup uses fixed
poses, one seeded item, flags, lethal HP and a deliberate authority pause to queue
two pickups in the same batch. This is not profession or tower-combat parity.

Measured hold expiry was 282.1 / 282.3 / 280.4 ms after the last server refresh in
the three lanes: 250 ms plus the 20 Hz sampling/frame allowance. Both clients lost
the temporary shared Heart source after its two-second linger and publication/
transport delay. Runtime expiry was not extended for network delay.

Echo medians A/B were 133.0/140.6 ms (100 ms Assault), 133.4/132.9 ms (100 ms Seal)
and 183.1/199.3 ms (150 ms Seal), with 114 echo samples per client. Recorded objective
acknowledgement p95 values range 466.1-532.9 ms over 3-10 requests per client and
include the deliberately delayed simultaneous-pickup fixture. These observations
do not pass or certify the normal-play 250 ms p95 gate; a separate representative
measurement without that fixture pause remains B36. Combat regression p95 was
232.7/216.0 ms over six action receipts each at measured 182.8/183.0 ms echo RTT.

Live Computer Use review at 1080p showed carried inventory, native Guild/Heart
markers, fog/minimap, confirmed claim progress and the consumed-Seal winning state.
At 720p, native Host travel, the HUD, separate fixture panel, one-item seed feedback
and a defence flag update were reviewed without clipping. The seed/flag clicks
were logged in 20261018-171520-610-Standalone/Standalone.log. Computer Use snapshots
were inspected live; no screenshot-readback instrumentation was added to gameplay.
Complete human recognition/readability acceptance remains open.

Initial attempts exposed two probe errors: the hidden-item fixture was still
legitimately inside lingering Heart sight, and delayed phase transitions were
confused with the next hold's expiry / checked source loss before transport could
arrive. The probes now isolate the new token and allow publication/transport for
the client source check. A real drop query also treated the dying pawn as blocked
ground; it now queries static-world objects and the valid-ground death/logout
lanes no longer use fallback. Failed attempts are retained locally and excluded
from the passing evidence above.

## Historical B34 verification

Combat/life implementation passes the checks below. Full v0.3 acceptance remains
open at that checkpoint for B35 transactions and B36 delivery, human review and broader measurements.

| Check | Result | Evidence under Artifacts/ |
| --- | --- | --- |
| Editor build | Passed | 20261018-162016-457-BuildEditor/BuildEditor.log |
| Separate Game target | Passed | 20261018-162227-908-BuildGame/BuildGame.log |
| Native rule/schema cases | 7 passed | 20261018-162227-890-Automation/Report/index.json |
| Combat world scenario | 48 passed | 20261018-162423-494-CombatCheck/Combat.json |
| Movement regression | 7 passed; all 8 residents moved | 20261018-162227-916-MovementCheck/Movement.json |
| B33 restricted-view regression | Server 12 / A 29 / B 27 passed; max payload 789 / 722 bytes | 20261018-162423-525-ViewNetworkCheck/{Server,A,B}.json |
| Combat network, rendered 1080p B | Server 9 / A 16 / B 19 passed | 20261018-162108-316-CombatNetwork-0-0/{Server,A,B}.json |
| Configured 100 ms RTT, +/-25 ms one-way jitter | Server 9 / A 16 / B 19 passed | 20261018-161422-563-CombatNetwork-100-25/{Server,A,B}.json |
| Configured 150 ms RTT, +/-25 ms one-way jitter | Server 9 / A 16 / B 19 passed | 20261018-161617-791-CombatNetwork-150-25/{Server,A,B}.json |
| Bootstrap Validate | Passed; all 23 package hashes unchanged | 20261018-162423-570-Bootstrap-Validate/Bootstrap.log |
| Reopened editor / MCP | Sandbox loaded, All Saved; 67 toolsets and editor state call succeed | B34-MCP/; docs/MCP_TEST_REPORT.md |

The combat world scenario verifies committed basic aim, windup/cadence, one-target
hits, assassination rear/range/turn/sprint/history checks, earlier versus same-batch
damage, recovery expiry, priority, duplicate requests, ward damage, no resident
reward, one-time loss, fresh return identity and protection. Its deadline cases
drive the resolver with controlled times. Its dash cases use real Character
Movement physics: open displacement 360.00 cm, wall/tree collision, ramp traversal
and ledge blocking, including dash cancellation without cooldown refund.

The network scenario uses actual controller RPCs, a separate authority and two
restricted clients. It checks warning receipt before assassination damage,
interruption, autonomous-owner dash reconciliation, private HP/loss, surviving
owned ward sight while genuinely unpossessed, fog on the waiting camera, a natural
20-second return, fresh observation/life identity, protection/stale-life rejection
and fighting afterward. Every phase audits canonical body/state and server-only
combat/ward/corpse actors. Test poses and state resets are labelled non-shipping
fixtures; damage, requests, waits, return ownership and view filtering are real.

Unreal's outgoing packet delay is configured on both endpoints: 25-75 ms for the
100 ms lane and 50-100 ms for the 150 ms lane. Logs confirm settings. Echo median
RTT was A/B 149.5/133.1 ms and 183.1/183.0 ms respectively (72 samples each).
Echo p95 was 166.4/182.9 ms and 216.3/216.4 ms. Six scripted action receipts per
client had p95 183.0/166.4 ms and 249.7/249.7 ms. Action and echo latency are separate
measurements; the 150 ms lane has little remaining margin. This small functional
sample does not certify the full acknowledgement distribution, snapshot latency,
packet loss, capacity, cooked-client performance or later interaction latency.

Live Computer Use review showed the active HUD/markers at 720p and the actual
death countdown, 21-gold receipt, ward-only sight and corpse presentation at 1080p,
without HUD clipping. The 1080p rendered scenario completed all functional checks.
Human warning recognition and full combat/interaction readability remain pending.

A diagnostic screenshot-readback attempt stalled both rendered clients for about
30 seconds and invalidated that run (20261018-161731-223-CombatNetwork-0-0).
That capture instrumentation was removed; the successful live lane renders one
client at a time. An earlier harness exited clients before the authority's final
assertion; a final phase notification fixed the shutdown ordering. One world probe
timed out waiting for the concurrent Game build's global UBT validation lock; it
passed after the build. These failed attempts are not counted as gameplay passes.

## Historical B33 verification

This checkpoint added private observation. It did not certify full v0.3.

| Check | Result | Evidence |
| --- | --- | --- |
| Editor build | Passed | Artifacts/20261018-133514-262-BuildEditor/BuildEditor.log |
| Game target build | Passed without editor dependencies | Artifacts/20261018-133526-702-BuildGame/BuildGame.log |
| Native rules / wire schema | 6 passed, 0 failed | Artifacts/20261018-132614-781-Automation/Report/index.json |
| Generated content | 23 packages; b33.1; 12 geometry actors per map | Artifacts/20261018-132524-068-Bootstrap-Regenerate/Bootstrap.log |
| Validate | Passed; aim/vision channels and elevation tag; all 23 hashes unchanged | Artifacts/20261018-133637-660-Bootstrap-Validate/Bootstrap.log |
| Movement regression | All 7 checks passed; all 8 residents moved | Artifacts/20261018-133706-409-MovementCheck/Movement.json |
| Separate authority plus 2 clients | Server 12, client A 29, client B 27 checks; 0 failures | Artifacts/20261018-133637-600-ViewNetworkCheck/Server.json, A.json, B.json |
| Serialized view payload | Maximum A=856 bytes; B=721 bytes, excluding transport framing | Same client JSON reports |

Each of nine phases audited actual restricted-client actors: one owned canonical
character, PlayerState, controller and relay; no resident, foreign canonical body,
foreign owner state, ward, AI controller or vision director. Received neutral
records were captured separately for each client. Ward sight benefited its owner;
public sight benefited both; loss removed dynamic records while retaining explored
terrain. Per-connection opaque IDs reject cross-owner and newly hidden lookups.

The scenario exercised reappearance, a delayed old full view, delayed concealment,
a publication pause exceeding 250 ms, recovery, waiting-state sources and camera
invariance. Missing views are simulated by withholding full publication; reordering
uses previously received messages passed through the actual receiver. This is not
an engine packet-loss, measured latency or performance benchmark. Ward expiry was
shortened by an authority fixture after validating its real 60-second initial
lifetime. No server-role identity table is shipped in the normal map.

The first network attempt timed out because engine-generated nicknames replaced
requested URL names. The fixture now identifies each test connection through an
owning-controller handshake, enabled only by the non-shipping diagnostic flag.
Production observed records contain no diagnostic identity, object references,
private stats or human/NPC role fields.

The first-publication source-loss rule is enforced by rebuilding permitted records
from current sources before sending; the scenario inspects authority outgoing data
after the transition and the resulting restricted views. No zero-latency client
concealment claim is made. Six rule/schema tests also cover bounded serialization,
round trips, world-cell boundaries and revision ordering.

## Historical B32 evidence and current acceptance table

# Checkpoint verification

18 October 2026, Asia/Shanghai. Historical scope: B30 audit, B31 foundation and B32 playable
movement checkpoint. This report does not certify the complete v0.3 release.

Compiler/SDK blockers are resolved. Editor and separate Game target builds pass
with x64 MSVC 14.44.35229, Windows SDK 10.0.26100.0 and Framework SDK 4.8.
The four native rule cases pass. Seven standalone world movement checks pass.

## Executed

| Check | Result | Evidence |
| --- | --- | --- |
| Web baseline unit suite | Passed, 89 tests | Artifacts/BaselineVerification/UnitTests.log |
| Web baseline typecheck | Passed | Artifacts/BaselineVerification/Typecheck.log |
| Baseline export | Pinned clean checkout, actual C/MAP | Config/BaselineSnapshot.json |
| Native prerequisites | Compiler and both SDKs detected | Artifacts/20261018-122214-031-Preflight/Environment.json |
| Editor build | Passed after UI/lighting review fixes | Artifacts/20261018-130509-791-BuildEditor/BuildEditor.log |
| Game target build | Passed without editor module | Artifacts/20261018-130522-546-BuildGame/BuildGame.log |
| Native rules | 4 succeeded, 0 failed/not run/in process | Artifacts/20261018-125418-743-Automation/Report/index.json |
| Bootstrap Regenerate | 21 genuine saved packages; b32.2 | Artifacts/20261018-130107-554-Bootstrap-Regenerate/Bootstrap.log |
| Bootstrap Validate | Passed, all 21 hashes unchanged | Artifacts/20261018-130305-545-Bootstrap-Validate/Bootstrap.log |
| World movement | 7 checks; walk/sprint, wall, ramp, cliff, crowd spawn/navigation | Artifacts/20261018-130327-166-MovementCheck/Movement.json |
| Editor MCP | Connected, level load and PIE Start/Stop succeed | docs/MCP_TEST_REPORT.md; live review 18 October |
| Rendered entry | Frontend and Host-to-sandbox travel work in PIE | B32 UI review; standalone evidence below |

Logs are local ignored artifacts. Validate map loading can report RecastNavMesh
teardown warnings; the independent runtime probe verifies navigation. Initial
visual review found low-contrast buttons and equal forward-light priorities;
both were fixed and both native targets rebuilt before standalone review.

## Native acceptance gates

| ID | Status | Observation |
| --- | --- | --- |
| U01 | Passed | Actual baseline/environment audited, missing compiler dependency identified |
| U02 | Passed | Editor target builds with the pinned engine; no Live Coding; module build IDs match |
| U03 | Passed | Separate Lockdown Game target builds without editor dependencies |
| U04 | Passed |23 saved generated packages; signatures, input mappings, fog, collision, geometry and elevation validated without rewriting |
| U05 | Passed | Win64 Development build/cook/stage/archive; native Game runtime with IoStore/pak |
| U06 | Passed for local entry | Native packaged Host720p, independent Join1080p and Return to menu verified; uncooked timeout feedback also reviewed |
| U07 | Passed | SpatialConversion plus actual CMC world checks: 180 cm/s walk and 300 cm/s diagonal sprint |
| U08 | Passed for slice | Native walk/dash wall, tree, ramp and ledge checks; remote root-motion dash reconciles |
| U09 | Partial | Camera-relative input, aim, menu suppression and delayed CMC tested; human feel and full focus-loss acceptance remain |
| U10 | Partial | Packaged720p/1080p HUD/fog/receipts inspected; human warning recognition and readability acceptance remain |
| U11 | Passed automated slice | Basic combat works through two remote owning controllers, including delayed transport; human playtest remains |
| U12 | Passed automated slice | Warning, rear/range/turn/sprint/history checks, damage interruption and dash cancellation verified |
| U13 | Passed for implemented intents | Connection, match/life, bounded planar arguments, rate/priority and cached duplicates; stale life and hidden/cross-owner observed lookup rejected |
| U14 | Passed for slice | Simultaneous strikes and due assassination/counterhit survive same-batch damage; earlier damage interrupts |
| U15 | Passed | Native DeathCost test verifies exact arithmetic and boundaries; runtime life loop is separate |
| U16 | Passed for slice | One private ceiling loss receipt; duplicate death cannot charge or delay twice |
| U17 | Passed for slice | Natural return creates fresh pawn/life/observation, full HP, four-second protection; old-life commands rejected |
| U18 | Partial | Natural network return and stale hold expiry measured; native Logout removes holds/queued work and drops item without a winner; abrupt transport disconnect/resume policy remains B36 |
| U19 | Passed for slice | One/zero active contenders do not win; deadline without a legal scheduled claim returns No claim |
| U20 | Passed automated slice | Actual remote courier loop, private stages, completion damage, exact reward and duplicate/cooldown checks |
| U21 | Passed automated slice | One seeded item, simultaneous real pickup RPCs, death/drop/theft, corpse independence and stale-life rejection |
| U22 | Passed automated slice | All fifteen closed defence masks reject ordinary access; actual carrier bypass and all-clear Assault both win through remote holds |
| U23 | Passed automated slice | Ten-second claim, contest regardless access, movement/release/range/stale/damage resets, completion-batch ordering and deadline boundaries |
| U24 | Passed for B33 | Explicit plateau level; low into/across blocked; high down-ramp allowed; walls and trees block both levels |
| U25 | Passed for B33 | Generated aim/vision collision and elevation tags validated; actual world trace tests pass |
| U26 | Passed for B33 | Body, private ward and public source union verified on two restricted clients |
| U27 | Passed for B33 | Owner-only bounded relay; pointer-free approved schema; per-client received records captured |
| U28 | Passed for slice | Packaged two-remote-client view/combat/objective audits and periodic ten-minute restricted registry checks pass |
| U29 | Passed for B33 | Public source loss removes remote records and proxies; explored terrain persists |
| U30 | Passed for B33 | Older full/concealment ignored; 250ms receive gap clears proxies; fresh full snapshot recovers |
| U31 | Passed implemented cues | Role-neutral action/marker/corpse cues use permitted records; private warning has no attacker coordinates; no independent spatial combat sounds/trails |
| U32 | Passed for B34 waiting | Real unpossession removes body source; owned ward remains; waiting camera keeps fog; public source checked in B33 |
| U33 | Passed for slice | Real placement/charge/private sight/lifetime/expiry and single nearest-strike ward destruction |
| U34 | Passed for B33 | Eight authority residents navigate; neutral shared proxies/schema and restricted-client registry audits pass |
| U35 | Passed for slice | Labelled host-only seed and four toggles reviewed in native UI; non-shipping scaffold/authority guards and logged setup; no remote fixture RPC |
| U36 | Passed for installed-engine topology | Independent packaged Game clients plus headless listen authority; restricted view/combat/objective checks pass |
| U37 | Passed measured small slice |1080p60-second render sample and600-second activity;60FPS median,16.83ms frame p95,0.864ms authority combined p95; capacity deferred |
| U38 | Passed measured lane |100/150ms functional lanes;600-second150ms+/-25ms soak,893/896 receipts,249.60/241.13ms p95; narrow A margin, WAN/loss unverified |
| U39 | Passed automated world-reload lane | Ten packaged non-seamless reloads,84 checks; old actors/state/commands/receipts/views rejected;11 clean observations |
| U40 | Partial | Runnable package, scripts, manifest, ledger and measured report supplied; human sign-off, source-engine Server and remote delivery remain conditional |

Packaged dedicated server: **Conditional not available** in this checkpoint; only
an installed engine is identified and no compatible source-build lane is established.


Required next evidence: human movement/aim/warning/fog/interaction comparison at720p/1080p. WAN/loss, full population, remote Git/LFS delivery and source-engine Server remain outside the verified lane.

## Standalone rendered review

At 1280x720, the native frontend, shared humanoid, actual speed HUD and menu
rendered in a separate uncooked game process. Host entered the sandbox; H opened
the menu, Escape resumed, and the wheel changed camera distance and cursor facing.
A second independent local process joined 127.0.0.1:7777 using the UI; the host
logged Join succeeded and the client completed map travel with its owned character.
The client's Return to menu closed its network connection and reopened L_Frontend.

An unavailable-host test initially exposed a dropped failure message: Unreal
supplies a null world for pending connections. The handler now resolves the
pending driver's owning world context and rejects another instance's context.
After rebuilding both targets, the repeated 20-second timeout displayed a short
ConnectionTimeout message on the frontend. Failure text does not expose raw
network-driver internals in the product UI.

Evidence: Artifacts/20261018-130556-611-RenderedMovement/Standalone.log and
Failure-720p.png; Artifacts/20261018-130758-265-Standalone/Standalone.log and
Joined-720p.png. This is local uncooked entry/leave evidence. It does not establish
U28 private views, U36 packaged clients, full U09 focus-loss behavior, U10 combat/
fog readability at both target resolutions, or network performance under delay.

## B33 rendered review

A separate interactive 1280x720 game window showed the native frontend, readable
source/freshness HUD, fog material and minimap after shader compilation. F on
out-of-range ground displayed rejection; F on nearby clear ground placed the
ward, consumed the single charge and changed the private-source count to one.
The labelled host fixture changed the shared source from one to zero; explored
cells stayed dim while fresh sight contracted. No material compilation errors
were found in that run.

Evidence: Artifacts/20261018-133727-028-Standalone/Standalone.log,
VisionWard-720p.png and PublicOff-720p.png. These are automated rendered checks,
not a human usability acceptance or combat/readability test.

A second interactive review at 1920x1080 also showed the source HUD, humanoids,
current/unknown fog and distant public/body sight regions on the minimap without
clipping. Evidence: Artifacts/20261018-134056-815-Vision1080/Vision-1080p.png and
Standalone.log. Full human usability and later combat/interaction readability
acceptance remain pending. Both review processes were closed normally.
