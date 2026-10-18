# Continuation checkpoint - 18 October 2026

**Current stage: B32 playable movement foundation; B33 filtered observation next.**

## Verified implementation

- Cold Editor and separate Game target builds pass with x64 MSVC 14.44.35229,
  Windows SDK 10.0.26100.0 and Framework SDK 4.8. Engine: 5.8.3 CL58210709.
- Four native rule cases and seven standalone world movement checks pass:
  180 cm/s walking, 300 cm/s diagonal sprint, wall blocking, ramp traversal,
  cliff rejection, eight authority-spawned residents and all eight navigating.
- Bootstrap saves 21 real packages. Validate checks signatures, saved geometry,
  13 input mappings and actual navigation brush size without saving packages.
- Native movement prediction carries sprint through saved moves. Camera-relative
  input, walkable-ground aim, zoom, a shared humanoid, frontend and speed HUD exist.
- Canonical characters and PlayerState channels are owner-only. Residents are
  server-only. Remote proxies and full privacy evidence are pending B33.

See TestReport.md, B32_CHECKPOINT.md and DemoGuide.md. Logs remain in ignored
Artifacts/. The checkpoint does not establish full v0.3 acceptance.

## Next work

Follow B33 in v03/06_PORT_SLICE_AND_BUILD_PLAN.md before broader gameplay:
server contender records and owner state, sanitized world-view relays, neutral
remote proxies, source union, explored fog, private wards, public tower sensors,
view revisions, concealment and recovery. Prove information separation with two
remote clients and a separate authority world. Owner-only flags alone are not proof.

B34 adds combat, dash, assassination, damage batches, death and return; B35 adds
courier/Seal/Heart transactions; B36 supplies cooked independent-client,
performance, delay and delivery evidence.

## Workflow and local state

Close the editor for native changes. Run BuildEditor.ps1 and BuildGame.ps1.
For generator changes bump BootstrapLayout.json's version, explicitly Regenerate,
then Validate and compare hashes. Run RunMovementCheck.ps1 for world/collision/
navigation changes and RunTests.ps1 for rule changes. No further installation is
needed for the verified local build. Normal editor MCP remains enabled;
standalone -game tests disable its editor-only Python toolsets.

The web baseline remains clean at 74d3d3e884bf4d8130e1596148b45e50a409039c.
Its 89 tests and TypeScript typecheck passed. BaselineSnapshot.json records the
actual exported constants/map and source hashes.

The owner's local EngineAssociation=5.8 change in Lockdown.uproject and generated
.vsconfig are separate from the implementation commit. Preserve them. Ignored
Config/Local/Environment.json resolves this machine's engine path; do not commit
an absolute engine path or implicitly stage the owner's unrelated configuration.
