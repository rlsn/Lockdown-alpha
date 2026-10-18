# Project Lockdown — Unreal migration

Windows-first Unreal 5.8.3 migration of the v0.2 Grand Heart prototype. Design
authority lives in [docs/master](docs/master/README.md); release scope and gates
live in [docs/v03](docs/v03/README.md).

**Current checkpoint: B35 representative interactions. Courier collection and
paid delivery, real Seal carry/drop/theft and both ten-second Heart claim routes
build on native combat, life, owner-only observation and fog.** See [checkpoint](docs/B35_CHECKPOINT.md),
[tests](docs/TestReport.md) and [continuation](docs/CONTINUATION.md).
Packaging, reset/isolation and full acceptance remain B36.

## Setup

1. Install/associate Unreal **5.8.3 CL 58210709**. Do not upgrade the project.
2. Install an MSVC version accepted by the pinned engine. The owner's update
   now provides x64 14.51.36260 and 14.44.35229; the earlier compiler ban is resolved.
   Windows SDK 10.0.26100.0 is now detected. In Visual Studio Installer > Modify >
   Individual components, also install **.NET Framework 4.8 SDK**
   (`Microsoft.Net.Component.4.8.SDK`). This is distinct from
   the modern .NET SDK/runtime and the .NET Framework targeting pack.
3. Run `git lfs install` before checking out binary assets.
4. Resolve the engine through the project's local association, or set
   `$env:LD_UE_ROOT` in the current PowerShell session. Alternatively create
   ignored `Config/Local/Environment.json` with `{"engineRoot":"<your engine root>"}`.
5. Close the editor before the first successful cold C++ build or reflected
   class changes. The project now declares native modules; reopening requires
   those modules to build successfully.

```powershell
./Scripts/Preflight.ps1
./Scripts/BuildEditor.ps1
./Scripts/BuildGame.ps1
./Scripts/BootstrapAssets.ps1
./Scripts/BootstrapAssets.ps1 -Mode Validate
./Scripts/RunTests.ps1
./Scripts/RunMovementCheck.ps1
./Scripts/RunViewNetworkCheck.ps1
./Scripts/RunCombatCheck.ps1
./Scripts/RunCombatNetworkCheck.ps1 -RoundTripMs 150 -JitterMs 25
./Scripts/RunObjectiveCheck.ps1
./Scripts/RunObjectiveNetworkCheck.ps1 -RoundTripMs 150 -JitterMs 25
./Scripts/OpenEditor.ps1
```

Preflight, both builds, native rule tests and movement checks pass on the audited
machine. See docs/TestReport.md for the remaining stage gates. PowerShell 7 is
the tested script host. Per-run output and local machine paths go to ignored
`Artifacts/`. Scripts fail if the discovered engine differs from the lock.

The generator saves 23 packages: three maps, tuning, eleven input actions, a
13-key context, four surface materials, fog material/texture and a native UI wrapper.
The sandbox supports camera-relative movement, sprint, aim, zoom, eight residents,
private ward sight, shared tower/Heart sight, explored fog, combat/life and objective transactions. Host and direct-address
Join use native UI. See [play guide](docs/DemoGuide.md). Default generation creates
missing assets and validates existing ones; `-Mode Validate` writes no assets;
`-Mode Regenerate` explicitly replaces generated packages under
`/Game/Lockdown/Generated/`. Keep hand-authored assets outside that directory.

After runtime implementation and bootstrap validation, `PackageWin64.ps1` will
invoke BuildCookRun and retain a separate Development archive for each run.
Packaging has not run. A packaged dedicated server is conditional on a compatible
source-engine build; the installed-engine lane is not claimed to support it.

## Baseline

The original web repository remains separate and unmodified. Export its constants
and map using its installed `tsx` dependency:

```powershell
./Scripts/ExportBaseline.ps1 -BaselineRoot '<path to v0.2 repository>'
```

The snapshot records the full Git revision and SHA-256 of the inspected source
files. It requires a clean baseline checkout. See [baseline audit](docs/BaselineAudit.md)
and [test report](docs/TestReport.md) for observed results.
