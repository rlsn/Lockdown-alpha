# Project Lockdown — v0.4 development

Windows-first Unreal 5.8.3 project extending the Grand Heart prototype. Design
authority lives in [docs/master](docs/master/README.md); active release scope and
gates live in [docs/v04](docs/v04/README.md). Start with the
[documentation index](docs/README.md) and [current handoff](docs/CONTINUATION.md).

**Current checkpoint: B44 Charter and wallet theft; B40–B43 verified.** v0.4
adds three private class starts, six attributes, five-level point awards, derived
HP/damage/speed, retained development through death and a locked start roster.
The Windows Development checkpoint builds, cooks and passes native and packaged
authority/network/reset checks. See the
[implementation record](docs/v04/IMPLEMENTATION_REPORT.md) for evidence and limits.
The [B42 checkpoint](docs/v04/B42_IMPLEMENTATION.md) adds slots 1–4, a mixed
hotbar, persistent cooldowns, Step/Ambush/Spark/Patch and held utility dispatch.
The [B43 checkpoint](docs/v04/B43_IMPLEMENTATION.md) adds held station commits,
private previews, class learning, ranks, permanent subclasses and physical books.
The [B44 checkpoint](docs/v04/B44_IMPLEMENTATION.md) adds four Charter alternatives,
a retained private Witness, persistent humanoid purses and once-only Pickpocket
transfers. Next is B45's connected region, bounded rewards and authored NPCs.
Earned Seal acquisition remains in development.

Use the frontend's **Host v0.4 development lobby**, select a class, mark Ready and
start as the lobby host. One human is practice; the declared cap is 25 minutes.
This preview retains migration-map geometry and courier work. Press 1–4 for the
equipped skill; hold utility keys. Q/Space require equipped Ambush/Step. P opens
preparation and books. Charter markers show the public skill threshold; the HUD
shows your own score. Hold equipped Pickpocket behind an eligible humanoid.
Seal crafting remains pending.
Opening the sandbox without `?Profile=V04`
selects the explicit legacy profile, documented in the
[v0.3 implementation archive](docs/v03/implementation/README.md).

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
./Scripts/RunDevelopmentCheck.ps1
./Scripts/RunDevelopmentCheck.ps1 -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -Reset
./Scripts/RunDevelopmentCheck.ps1 -Preparation
./Scripts/RunDevelopmentCheck.ps1 -Preparation -Normal
./Scripts/RunDevelopmentCheck.ps1 -Preparation -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks -Normal
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks -Network -RoundTripMs 150 -JitterMs 25
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

The generator saves 24 packages: three maps, two data assets, eleven input actions, a
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
