# Project Lockdown — v0.4 preparation

Windows-first Unreal 5.8.3 project extending the Grand Heart prototype. Design
authority lives in [docs/master](docs/master/README.md); active release scope and
gates live in [docs/v04](docs/v04/README.md). Start with the
[documentation index](docs/README.md) and [current handoff](docs/CONTINUATION.md).

**Current code baseline: v0.3 B36; v0.4 implementation has not started.** The
existing Windows Development package includes courier work, Seal theft, Heart
claims, combat/life and private observation. See the archived
[checkpoint](docs/v03/implementation/B36_CHECKPOINT.md),
[tests](docs/v03/implementation/TestReport.md) and
[v0.4 baseline review](docs/v04/BASELINE_REVIEW.md). Next is B40 prerequisite
verification, then class development, shared skill slots and earned peaceful
acquisition. Human usability acceptance and conditional dedicated-server delivery
remain open.

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
./Scripts/RunResetCheck.ps1
./Scripts/PackageWin64.ps1
./Scripts/OpenEditor.ps1
```

Preflight, both builds, native rule tests and movement checks pass on the audited
machine in the archived v0.3 evidence. See the
[test report](docs/v03/implementation/TestReport.md) for its remaining stage gates.
PowerShell 7 is
the tested script host. Per-run output and local machine paths go to ignored
`Artifacts/`. Scripts fail if the discovered engine differs from the lock.

The generator saves 23 packages: three maps, tuning, eleven input actions, a
13-key context, four surface materials, fog material/texture and a native UI wrapper.
The sandbox supports camera-relative movement, sprint, aim, zoom, eight residents,
private ward sight, shared tower/Heart sight, explored fog, combat/life and objective transactions. Host and direct-address
Join use native UI. See the [v0.3 play guide](docs/v03/implementation/DemoGuide.md).
Default generation creates
missing assets and validates existing ones; `-Mode Validate` writes no assets;
`-Mode Regenerate` explicitly replaces generated packages under
`/Game/Lockdown/Generated/`. Keep hand-authored assets outside that directory.

`PackageWin64.ps1` runs BuildCookRun and retains a separate Development archive
and source/payload hash manifest for each run. It uses file-backed cooked output
and IoStore/pak containers. Open the archive with `OpenPackaged.ps1 -PackageRoot
'<archive>'`; the three network runners also accept `-PackageRoot`. Their packaged
server is a headless listen authority with two remote clients, not a dedicated
Server target. `RunSoakCheck.ps1 -PackageRoot '<archive>'` measures the small slice
for ten minutes with one rendered 1080p client. See the report for actual results.
A packaged dedicated server remains conditional on a compatible source engine.

`VerifyCleanCheckout.ps1` checks a committed local clone with assets hydrated from
the local LFS cache, both native builds, asset validation and rule tests. A GitHub
origin is configured; this local test does not certify remote Git/LFS distribution.

## Baseline

The original web repository remains separate and unmodified. Export its constants
and map using its installed `tsx` dependency:

```powershell
./Scripts/ExportBaseline.ps1 -BaselineRoot '<path to v0.2 repository>'
```

The snapshot records the full Git revision and SHA-256 of the inspected source
files. It requires a clean baseline checkout. See the
[baseline audit](docs/v03/implementation/BaselineAudit.md) and
[test report](docs/v03/implementation/TestReport.md) for observed results.
