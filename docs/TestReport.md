# B34 verification - 18 October 2026

Combat/life implementation passes the checks below. Full v0.3 acceptance remains
open for B35 transactions and B36 delivery, human review and broader measurements.

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
| U04 | Passed for B32 | 21 saved packages; signatures, mappings, brush size and geometry validated; all hashes unchanged |
| U05 | Not run | No cook/package |
| U06 | Passed for uncooked entry | Standalone UI Host, second-process Join, timeout feedback and client Return to menu verified |
| U07 | Passed | SpatialConversion plus actual CMC world checks: 180 cm/s walk and 300 cm/s diagonal sprint |
| U08 | Passed for slice | Native walk/dash wall, tree, ramp and ledge checks; remote root-motion dash reconciles |
| U09 | Partial | Native camera-relative input, walkable-ground aim, menu suppression and input flush implemented; full focus/network/vision evidence pending |
| U10 | Partial | Shared humanoid, facing and native UI render; combat cues, fog and full resolution review pending |
| U11 | Passed automated slice | Basic combat works through two remote owning controllers, including delayed transport; human playtest remains |
| U12 | Passed automated slice | Warning, rear/range/turn/sprint/history checks, damage interruption and dash cancellation verified |
| U13 | Passed for implemented intents | Connection, match/life, bounded planar arguments, rate/priority and cached duplicates; stale life and hidden/cross-owner observed lookup rejected |
| U14 | Passed for slice | Simultaneous strikes and due assassination/counterhit survive same-batch damage; earlier damage interrupts |
| U15 | Passed | Native DeathCost test verifies exact arithmetic and boundaries; runtime life loop is separate |
| U16 | Passed for slice | One private ceiling loss receipt; duplicate death cannot charge or delay twice |
| U17 | Passed for slice | Natural return creates fresh pawn/life/observation, full HP, four-second protection; old-life commands rejected |
| U18 | Not run | Native connection lifecycle pending |
| U19 | Not run | Objective-only runtime end conditions pending |
| U20 | Not run | Native courier pending |
| U21 | Not run | Native Seal transaction pending |
| U22 | Not run | Access helper supplied; real channel/request path pending |
| U23 | Not run | Deadline predicate supplied; actual channels pending |
| U24 | Passed for B33 | Explicit plateau level; low into/across blocked; high down-ramp allowed; walls and trees block both levels |
| U25 | Passed for B33 | Generated aim/vision collision and elevation tags validated; actual world trace tests pass |
| U26 | Passed for B33 | Body, private ward and public source union verified on two restricted clients |
| U27 | Passed for B33 | Owner-only bounded relay; pointer-free approved schema; per-client received records captured |
| U28 | Passed for uncooked topology | Nine actor-registry audits per client against a separate authority; no foreign canonical/state/AI/ward channels |
| U29 | Passed for B33 | Public source loss removes remote records and proxies; explored terrain persists |
| U30 | Passed for B33 | Older full/concealment ignored; 250ms receive gap clears proxies; fresh full snapshot recovers |
| U31 | Passed implemented cues | Role-neutral action/marker/corpse cues use permitted records; private warning has no attacker coordinates; no independent spatial combat sounds/trails |
| U32 | Passed for B34 waiting | Real unpossession removes body source; owned ward remains; waiting camera keeps fog; public source checked in B33 |
| U33 | Passed for slice | Real placement/charge/private sight/lifetime/expiry and single nearest-strike ward destruction |
| U34 | Passed for B33 | Eight authority residents navigate; neutral shared proxies/schema and restricted-client registry audits pass |
| U35 | Not run | Fixture authority/label UI pending |
| U36 | Not run | Uncooked local Host/Join passed; packaged clients and two-remote-client privacy tests pending |
| U37 | Not run | No native performance measurement |
| U38 | Partial | 100/150 ms configured RTT with +/-25 ms jitter; measured echo and six combat receipts per client; broader/cooked/transaction samples pending |
| U39 | Not run | No native reset/isolation test |
| U40 | Not run | Partial checkpoint documentation supplied; full delivery incomplete |

Packaged dedicated server: **Conditional not available** in this checkpoint; only
an installed engine is identified and no compatible source-build lane is established.


Required next evidence: B35 transactions and B36 package, human review and broader performance gates.

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
