# Runtime architecture and boundaries

B32 supplies a native movement sandbox and frontend. The complete v0.3
multiplayer information architecture is the next gate.

## Implemented

- Lockdown contains Character Movement, Enhanced Input, UMG, NavigationSystem/AI,
  pure LDRules helpers and validated prototype tuning. A separate Game target
  build verifies runtime code without UnrealEd.
- LockdownEditor owns bootstrap and package-save APIs; UnrealEd/UMGEditor stay
  in editor dependencies.
- ALDCharacter uses a 28 cm radius, 90 cm half-height capsule and a shared
  seven-part humanoid. ULDCharacterMovementComponent carries sprint through
  saved moves and a compressed flag. Walk/sprint speeds are 180/300 cm/s.
- ALDPlayerController handles camera-relative input, finite yaw intent and
  walkable LDGroundAim traces. Menu/focus changes clear movement and sprint.
- ALDGameMode draws from a shared spawn pool without replacement and starts
  eight nonviolent ALDResident actors. Residents choose reachable destinations.
  Player and NPC presentation use the same component and palette.
- ULDUIRoot supplies frontend/menu/HUD through a generated Widget Blueprint
  wrapper. Game startup is L_Frontend; editor startup is L_MigrationSandbox.
  ULDGameInstance handles travel/network failures and returns to the frontend.
- Canonical characters and PlayerState channels are owner-only. Resident bodies
  and AI controllers do not replicate. Remote relays/proxies remain pending.

Four rule cases cover conversion, death arithmetic, objective boundaries and
view ordering. A non-shipping command-line probe drives actual Character Movement
against saved collision, ramps and asynchronous navigation. It does not measure
rendering performance or real network latency.

## Generated content

BootstrapLayout.json defines 21 saved packages under /Game/Lockdown/Generated/.
The bootstrap constructs a real brush model and collision body for navigation.
Validate checks signatures, input count, saved meshes/transforms and bounds size
without writing packages. Generate fills absent packages; explicit Regenerate
is the overwrite mode. Metadata records generator version and authoring hash.
Hand-authored content belongs outside the generated directory.

LDGroundAim and LDVision separate walkable aim surfaces and opaque blockers.
The controller rejects steep aim normals. Collision channels alone do not
implement logical elevation or visibility policy.

## Next dependency gate

B33 adds contender records, owner state, sanitized owner relays and neutral local
proxies, then source union, explored fog, ward/tower sensors and view recovery.
Two restricted clients must receive different legal information without hidden
canonical bodies or owner mappings. Combat waits for that gate. No GAS, account
service, online subsystem or unrelated framework was added.
