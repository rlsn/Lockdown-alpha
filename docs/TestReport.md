# Checkpoint verification

18 October 2026, Asia/Shanghai. Scope: B30 audit, B31 foundation and B32 playable
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
| U08 | Partial | Ramp traversal and cliff blocking pass in world; dash deferred to B34 |
| U09 | Partial | Native camera-relative input, walkable-ground aim, menu suppression and input flush implemented; full focus/network/vision evidence pending |
| U10 | Partial | Shared humanoid, facing and native UI render; combat cues, fog and full resolution review pending |
| U11 | Not run | Native combat pending |
| U12 | Not run | Native assassination pending |
| U13 | Not run | Native request authority pending |
| U14 | Not run | Native batched impacts pending |
| U15 | Passed | Native DeathCost test verifies exact arithmetic and boundaries; runtime life loop is separate |
| U16 | Not run | Exact loss helper supplied; owner-private runtime pending |
| U17 | Not run | Native life replacement pending |
| U18 | Not run | Native connection lifecycle pending |
| U19 | Not run | Objective-only runtime end conditions pending |
| U20 | Not run | Native courier pending |
| U21 | Not run | Native Seal transaction pending |
| U22 | Not run | Access helper supplied; real channel/request path pending |
| U23 | Not run | Deadline predicate supplied; actual channels pending |
| U24 | Not run | Native logical elevation/vision pending |
| U25 | Not run | Trace channels defined; world geometry/visibility tests pending |
| U26 | Not run | Native source union pending |
| U27 | Not run | Owner relay payload pending |
| U28 | Not run | Restricted-client actor audit pending |
| U29 | Not run | Concealment runtime pending |
| U30 | Not run | Ordering helper/cases supplied; transport/proxy timeout tests pending |
| U31 | Not run | Native secondary cue filtering pending |
| U32 | Not run | Native waiting view pending |
| U33 | Not run | Ward tuning supplied; runtime pending |
| U34 | Partial | Eight authority residents spawned; all eight navigate; shared visual component; restricted-client proxies pending B33 |
| U35 | Not run | Fixture authority/label UI pending |
| U36 | Not run | Uncooked local Host/Join passed; packaged clients and two-remote-client privacy tests pending |
| U37 | Not run | No native performance measurement |
| U38 | Not run | No native delay measurement |
| U39 | Not run | No native reset/isolation test |
| U40 | Not run | Partial checkpoint documentation supplied; full delivery incomplete |

Packaged dedicated server: **Conditional not available** in this checkpoint; only
an installed engine is identified and no compatible source-build lane is established.


Required next evidence: B33 filtered views and restricted-client audits before combat,
then remaining life/objective/package/performance gates.

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
