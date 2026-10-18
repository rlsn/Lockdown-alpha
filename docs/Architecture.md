# Foundation architecture and boundaries

This checkpoint contains source for the initial build and bootstrap gate only.
None of the native source has compiled because the toolchain was rejected before
C++/UHT execution. This file distinguishes supplied code from future stages.

## Supplied

- `Lockdown` runtime module has Core/CoreUObject/Engine/InputCore/EnhancedInput
  dependencies, with pure `LDRules` helpers and `ULDPrototypeTuning` data validation.
- `LockdownEditor` owns the asset bootstrap commandlet and UnrealEd dependency.
  Runtime code does not reference UnrealEd or package-save APIs.
- Four `Lockdown.Rules` automation cases cover conversion, death arithmetic,
  objective deadline/access boundaries and view ordering. They have not run.
- Bootstrap source reads `Config/BootstrapLayout.json`, saves real packages through
  Unreal APIs and checks generated signatures/geometry. It creates three map
  foundations, tuning, ten input actions and a partial mapping context. It does
  not create a functioning frontend, character or completed gameplay map.
- Collision channels `LDGroundAim` and `LDVision` separate walkable aim surfaces
  from opaque sight blockers. Exact high-ground policy and navigation follow in B32/B33.
- The default startup map is retained until generated maps exist and pass their
  asset gate; configuration does not point at absent binary assets.

Generated packages stay under `/Game/Lockdown/Generated/`. Validate mode never
creates/replaces assets. Generate only fills absent packages; existing packages
are validated. Explicit Regenerate is the only overwrite mode. Metadata records
generator version and authoring-data hash. Geometry validation checks expected
labels, meshes and transforms; real collision and ramp traversal still need world
tests. Hand-authored content belongs outside this generated directory.

## Next dependency gates

B31 requires a cold Editor build, real bootstrap output, repeated validation and
native rule-test results. Then B32 adds Character Movement, camera-relative
Enhanced Input, humanoid presentation, native HUD, navigation and eight residents.
B33 establishes owner-only canonical characters, server-only residents and
sanitized owner relays with neutral local proxies before adding broader combat.
This order follows the release specification; no CMC/replication workaround is
being presented as a completed privacy architecture.

No framework, GAS, online subsystem, public service, account system or additional
content backlog was added. AllToolsets remains an editor tool dependency chosen
by the owner.
