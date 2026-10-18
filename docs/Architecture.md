# Runtime architecture and boundaries

B32 supplies native movement; B33 adds private observation; B34 adds combat and
life. See B33_CHECKPOINT.md and B34_CHECKPOINT.md for boundaries and evidence.

## Implemented

- Lockdown contains Character Movement, Enhanced Input, UMG, NavigationSystem/AI,
  pure LDRules helpers and validated prototype tuning. A separate Game target
  build verifies runtime code without UnrealEd.
- LockdownEditor owns bootstrap and package-save APIs; UnrealEd/UMGEditor stay
  in editor dependencies.
- ALDCharacter uses a 28 cm radius, 90 cm half-height capsule and a shared
  seven-part humanoid plus a role-neutral status marker. ULDCharacterMovementComponent carries sprint through
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

Seven rule cases cover spatial/death/objective arithmetic, view ordering, bounded
serialization, privacy schema and combat geometry. Separate non-shipping probes
exercise movement/navigation, combat physics and restricted-client network flows.

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

## Observation and combat

ALDVisionDirector samples after physics and constructs each connection's permitted
view. ALDWorldViewRelay sends bounded full views and reliable concealment.
ALDObservedEntityProxy is local and nonreplicated. LDViewTypes contains the
approved pointer-free wire schema. Owner resources stay on the owning controller;
public sensor availability stays on GameState. The postprocess mask and minimap
use received current/explored cells. Canonical meshes on a listen host are hidden
except its own body, so the host also renders observed crowd proxies.

ALDCombatDirector owns private body combat state, a bounded priority intent queue,
due impact collection, simultaneous damage and resident returns. ALDGameMode owns
persistent contender records and one-time death/return receipts. Vision ticks after
combat. Dash uses a server root-motion source in Character Movement. The controller
replicates only private self state and revisioned reliable life/warning receipts.
Corpses remain server-only and enter views as neutral presentation records.

No GAS, online account service, custom Replication Graph or second fog/filter
framework was introduced. B35 transactions are the next implementation gate.
