# Runtime architecture and boundaries

B32 supplies native movement; B33 adds private observation. See B33_CHECKPOINT.md
for the wire format, source policy and executed privacy evidence.

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
  and AI controllers do not replicate. B33 supplies owner relays, neutral proxies,
  source union, explored fog and a private ward.

Four rule cases cover conversion, death arithmetic, objective boundaries and
view ordering. A non-shipping command-line probe drives actual Character Movement
against saved collision, ramps and asynchronous navigation. It does not measure
rendering performance or real network latency.

## Generated content

BootstrapLayout.json defines 23 saved packages under /Game/Lockdown/Generated/.
The bootstrap constructs a real brush model and collision body for navigation.
Validate checks signatures, input count, saved meshes/transforms and bounds size
without writing packages. Generate fills absent packages; explicit Regenerate
is the overwrite mode. Metadata records generator version and authoring hash.
Hand-authored content belongs outside the generated directory.

LDGroundAim and LDVision separate walkable aim surfaces and opaque blockers.
The controller rejects steep aim normals. Collision channels alone do not
implement logical elevation or visibility policy.

## B33 observation and next gate

ALDVisionDirector samples after physics and constructs each connection's permitted
view. ALDWorldViewRelay sends bounded full views and reliable concealment.
ALDObservedEntityProxy is local and nonreplicated. LDViewTypes contains the
approved pointer-free wire schema. Owner resources stay on the owning controller;
public sensor availability stays on GameState. The postprocess mask and minimap
use received current/explored cells. Canonical meshes on a listen host are hidden
except its own body, so the host also renders observed crowd proxies.

B34 adds combat/life through these existing boundaries. No GAS, online account
service, custom Replication Graph or second fog/filter framework was introduced.
