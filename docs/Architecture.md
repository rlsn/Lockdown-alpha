# Runtime architecture and boundaries

B32 supplies native movement; B33 adds private observation; B34 adds combat and
life; B35 adds courier, Seal and Heart transactions. See the corresponding
checkpoint documents for boundaries and evidence.

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

Eight rule cases cover spatial/death/objective arithmetic, held/deadline boundaries, view ordering, bounded
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

ALDObjectiveDirector runs inside the combat sample after impacts, deaths and
returns. It commits queued interactions and work before claim/deadline evaluation.
Held tokens refresh at 10 Hz, expire after 250 ms, and cannot be revived by a late
refresh after release. Movement intent, physical movement, legal combat and damage
reset progress. Courier contracts and balances live only in the owner record;
bounded retry receipts prevent repeated rewards.

ALDSeal is server-only with one immutable item identity and dropped/carried/consumed
states. Only a currently visible dropped pose enters connection-scoped observations.
Pickup resolves that observed ID again on authority. Same-sample transactions use
an independent private shuffle. Death/logout drops the item on validated navigation
ground; its lifetime is independent of the corpse. Broad district notices contain
no carrier identity or coordinates. Native client-local landmarks describe public
Guild, West station and Heart locations without opening canonical actor channels.

GameState publishes defence flags, aggregate claim state, district notices and one
terminal receipt. Exact work, inventory, confirmed progress and winner-self state
remain private. A held claim contributes shared Heart sight, with two-second linger.
Completion is checked after damage and before the twelve-minute deadline; scheduled
completion must be at or before that deadline. Results freeze competitive activity
and source-rule time until leaving the world. Complete rematch cleanup is B36.

No GAS, online account service, custom Replication Graph or second fog/filter
framework was introduced. B36 packaging and complete acceptance are next.
