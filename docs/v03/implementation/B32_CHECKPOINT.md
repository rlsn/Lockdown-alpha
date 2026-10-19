# B32 movement checkpoint

18 October 2026. This is the first playable movement foundation of v0.3; combat,
fog, private observation relays and objective transactions are not implemented.

## Implemented

- Native Character Movement, capsule radius 28 cm and half-height 90 cm.
  Explicit acceleration/braking 1800 cm/s², maximum step 35 cm, walkable slope 40°.
- Camera-relative WASD through a saved Enhanced Input context. Diagonal input is
  clamped to unit length. Walk is 180 cm/s; sprint is 300 cm/s. Sprint uses a
  saved-move compressed flag and is restored during prediction/replay.
- Cursor traces only the LDGroundAim channel and accept walkable normals.
  Invalid traces retain the last facing. Clients send finite yaw intent at 10 Hz;
  the server bounds accepted updates to at most 25 Hz. No target position is trusted.
- Fixed camera yaw -45°, pitch -60°, FOV 55°, spring arm 1400 cm; zoom clamps to
  1100–1700 cm. Camera movement does not add a vision source.
- Shared original humanoid presentation with facing, palette and walk animation.
  Residents and controlled characters use the same visual component and palette.
- Eight nonviolent, authority-spawned residents navigate legal random destinations.
  Spawn locations are drawn without replacement from a shared pool with server-local
  randomness. No human/NPC identity table is placed in a client-loaded map.
- A native UMG frontend/HUD and generated WBP_UIRoot wrapper provide Host, Join
  address, connection feedback, movement controls, menu, return and Exit. The HUD
  labels this a movement checkpoint and does not advertise unimplemented actions.
- Menu opening suppresses movement and releases sprint. Viewport focus loss uses
  the engine's input flush policy and the controller's FlushPressedKeys override.
- Canonical characters and PlayerState channels are owner-only; resident bodies
  do not replicate. Full view relays/proxies and privacy verification follow in B33.
  Remote crowds are not presented as complete multiplayer functionality yet.

## Assets and bootstrap

Generator b32.2 saves 21 packages: three maps, tuning, eleven input actions,
the 13-key context, four materials and the UI wrapper. It constructs an actual
brush model/body for a 4800×3200×800 cm navigation volume. Validate checks the
saved volume size, authored meshes/transforms, input count, asset classes and
generation metadata. Existing packages are fully loaded before regeneration.

The native menu is the game startup map. The migration sandbox is the editor
startup map. The prototype tuning remains anchored to baseline revision 74d3d3e.

## Executed movement scenario

Scripts/RunMovementCheck.ps1 launches a standalone game world with fixed simulation
steps and NullRHI. It waits for navigation readiness in real time before driving
a separate test character through Character Movement. It inspects the generated
world, not a handwritten collision approximation. This is a correctness scenario,
not a frame-rate or real network latency benchmark.

All seven checks passed in
Artifacts/20261018-125120-618-MovementCheck/Movement.json:

| Check | Observation |
| --- | --- |
| Authority crowd | Eight residents spawned |
| Walking | 180.0 cm/s; 217.5 cm over the timed phase including acceleration |
| Diagonal sprint | 300.0 cm/s; 352.8 cm over the timed phase |
| Wall | Character blocked at the wall face |
| Ramp | Character reached plateau; capsule centre Z=292.128 cm |
| Cliff | Character could not step onto the plateau directly |
| Resident navigation | All eight moved more than 30 cm |

The first scenario exposed a zero-size brush volume; generation was fixed and
validation now rejects that defect. A warm-up gate prevents asynchronous navigation
startup from being mistaken for a movement result. Editor toolsets are disabled
only for the standalone -game probe, avoiding editor Python startup under a game
world. They remain enabled in the normal editor.

Rendered UI/input review and separate network evidence are recorded in TestReport.md
as they are completed. This checkpoint alone does not certify U01–U40 or full v0.3.
