# Project Lockdown — v0.9 B92 population and economy in progress

B91 is complete at its functional-environment boundary. B92 now connects 24
persistent contender seats and 196 role bodies to five physical commodity
markets and one world/market item ledger. Two remote owners and replacement-seat
takeover pass connected checks; routines, security, work, rare services and
complete-match acceptance remain open. See the [current service review](docs/v09/B92_SERVICES.md),
[roster review](docs/v09/B92_ROSTER.md) and [active handoff](docs/CONTINUATION.md).

The [banking extension](docs/v09/B92_BANK.md) now connects one persistent bank
account to all five counters, including fee-bearing deposit and cross-city
withdrawal after remote takeover. Shared official stock, work and living-world
behavior remain in progress.

The full-map work starts from the native v0.8 B86 baseline. B90 adds a
deterministic 2.4 km terrain/river/bridge/settlement skeleton, 96 stable POIs,
144 building blocks, 24 spawn pads and native navigation evidence. B90's spatial
stage is complete, including settlement/service mapping, fitted road/doorway
geometry, native travel heat and actual world-reset/profile isolation. Its
standalone spatial preview is separate from full-match gameplay, which still
requires the remaining B92–B94 work.

Start with the [active handoff](docs/CONTINUATION.md),
[B91 functional-environment progress](docs/v09/B91_IMPLEMENTATION.md),
[finite storage and cargo](docs/v09/B91_OBJECTS.md),
[portable tools and retained fuel](docs/v09/B91_TOOLS.md),
[approved ladder/rope traversal](docs/v09/B91_TRAVERSAL.md),
[fixed furniture and storage](docs/v09/B91_FURNITURE.md),
[finite sapling progress](docs/v09/B91_SAPLINGS.md),
[paid full-map fields](docs/v09/B91_FIELDS.md),
[Torch socket ignition](docs/v09/B91_IGNITION.md),
[B90 completion and evidence](docs/v09/B90_COMPLETION.md),
[v0.9 stage plan](docs/v09/09_IMPLEMENTATION_STAGES.md), and
[living game design](docs/master/README.md). The master now covers vision,
principles, core loops, player journeys, mechanics, control/UI, map, population,
economy and long-term development decisions.

```powershell
python -B Scripts/SyncV09Data.py --check
./Scripts/BuildB90Map.ps1 -Validate
./Scripts/OpenB90.ps1
```

The first B91 increment adds fitted interiors, permanent stairs and authoritative
doors, with restricted two-owner observation and native recordings. The next
checkpoint adds finite storage, inherited flasks and crate/barrel cargo:

```powershell
python -B Scripts/SyncB91Data.py --check
python -B Scripts/SyncB91Objects.py --check
python -B Scripts/SyncB91Tools.py --check
python -B Scripts/SyncB91Traversal.py --check
./Scripts/OpenB91.ps1 -Visible
```

## Retained v0.8 playable integration

Windows-first Unreal 5.8.4 project. The explicit v0.8 profile now exposes all
52 catalog abilities, 80 items and 20 recipes, with native elemental/support
effects, equipment modifiers/procs and paid acquisition. Six mixed-source
actions, eleven physical bindings and nine class starts remain in place.

Start with the [active handoff](docs/CONTINUATION.md),
[B86 implementation and evidence](docs/v08/B86_IMPLEMENTATION.md),
[v0.8 B80–B86 build plan](docs/v08/11_IMPLEMENTATION_PLAN_AND_RELEASE_SCOPE.md)
and [working ledger](docs/v08/FeatureLedger.csv). The enabled slice has
52 abilities/80 items/20 recipes/all nine presets. B85 demonstrates all four
earned Heart routes, a level-one tactical interruption and complete ledger
reconciliation. B86 adds source-aware native commands, inspection/help, remapping evidence and
a fresh packaged four-client workload. See the report for measured outcomes
and remaining acceptance.

```powershell
./Scripts/OpenStandalone.ps1 -Profile v08_integration
python -B Scripts/SyncB84Data.py --check
./Scripts/RunWorldCheck.ps1 -Profile v08_integration
./Scripts/RunWorldBoundaryCheck.ps1 -Profile v08_integration
```

See the implementation report for current native validation and evidence.
Human readability, physical-device/OS input, full adverse matrices and full
release acceptance remain open. The recorded whole-service proxy exceeds the
provisional performance budget; native automation is scoped separately. The separate `v07_integration` profile retains the
[B76 runtime](docs/v07/B76_IMPLEMENTATION.md), including all four Heart routes.
[B77 closeout](docs/v07/B77_CLOSEOUT.md) preserves scoped acceptance and named
deferrals. The default frontend / `v06_regression` remain on V05.
[B84](docs/v08/B84_IMPLEMENTATION.md), [B83](docs/v08/B83_IMPLEMENTATION.md), [B82](docs/v08/B82_IMPLEMENTATION.md), [B81](docs/v08/B81_IMPLEMENTATION.md) and
[B80](docs/v08/B80_BASELINE_REVIEW.md) preserve earlier checkpoints;
[masterR7](docs/master/README.md) remains long-term context.

[B64 institutions](docs/v06/B64_IMPLEMENTATION.md), [B63 production](docs/v06/B63_IMPLEMENTATION.md),
[B62 commerce](docs/v06/B62_IMPLEMENTATION.md) and [B61 foundations](docs/v06/B61_IMPLEMENTATION.md)
record retained checkpoints. [B60 review](docs/v06/B60_BASELINE_REVIEW.md) adopts
master revision 6 and the complete economy spec against the actual B57 runtime.

**Retained v0.5: B51 navigation through B57 presentation implemented.**
[B58 handoff](docs/v05/B58_HANDOFF.md) records retained evidence and open acceptance.
The user requested minimal verification before moving to the next version.
The [B50 baseline review](docs/v05/B50_BASELINE_REVIEW.md) records the audited
baseline and defaults. [B51](docs/v05/B51_IMPLEMENTATION.md) proves native
multiplayer navigation; [B52](docs/v05/B52_IMPLEMENTATION.md) adds saved remapping,
explicit targeting and fresh-input panel routing. [B53](docs/v05/B53_IMPLEMENTATION.md)
adds the masked inspector, anchored own commands, interactive filtered minimap
and life/results presentation. [B54](docs/v05/B54_IMPLEMENTATION.md) adds the 24-slot
bag, finite ward instances and four real quick-item references. [B55](docs/v05/B55_IMPLEMENTATION.md)
adds the quoted Guild bookshop beside your own bag. [B56](docs/v05/B56_IMPLEMENTATION.md)
adds voluntary item/gold exchange with reservations, Ready and separate Confirm. The ordinary
frontend now launches the v0.6 economy through B66. [B57](docs/v05/B57_IMPLEMENTATION.md) adds shared panels, compact command rows and keyboard review/focus. Extended release verification is carried forward explicitly; v0.6 scope is now recorded in the B60 review.

**Retained runtime: B47 recorded; full stress validation deferred by user.** v0.4
adds three private class starts, six attributes, five-level point awards, derived
HP/damage/speed, retained development through death and a locked start roster.
The checkpoint passes native and cooked authority, network, ordinary-input and reset checks.
See the
[implementation record](docs/v04/IMPLEMENTATION_REPORT.md) for evidence and limits.
The [B42 checkpoint](docs/v04/B42_IMPLEMENTATION.md) adds slots 1–4, a mixed
hotbar, persistent cooldowns, Step/Ambush/Spark/Patch and held utility dispatch.
The [B43 checkpoint](docs/v04/B43_IMPLEMENTATION.md) adds held station commits,
private previews, class learning, ranks, permanent subclasses and physical books.
The [B44 checkpoint](docs/v04/B44_IMPLEMENTATION.md) adds four Charter alternatives,
a retained private Witness, persistent humanoid purses and once-only Pickpocket
transfers. The [B45 checkpoint](docs/v04/B45_IMPLEMENTATION.md) adds two saved
regional topologies, bounded courier/visit/sentinel rewards, an autonomous Patch
NPC and actual watchtower sabotage. The [B46 checkpoint](docs/v04/B46_IMPLEMENTATION.md)
adds atomic earned fabrication and recovery through the real Heart route.
The [B47 record](docs/v04/B47_IMPLEMENTATION.md) records core verification,
the passing 25-minute run and corrected sampling/dash timing. The 90-minute
stress case was stopped at user direction; a fresh B47 cold checkout and human
review remain pending for the next version. Use
the [review guide](docs/v04/B47_REVIEW_GUIDE.md) for actual multiplayer sessions.

Use **Host private lobby**, select a class, mark Ready and start as
host. One human is practice; the retained cap is 25 minutes. Right-click moves or
approaches, S stops, Shift sprints, left-click inspects, and A then world-click
attacks. Q/W/E/R invoke equipped skill modes; hold the initiating key/button for
channels. F performs context, P opens character/preparation and F1 opens saved
controls. Arrows/edge pan, held Space, Home and the wheel control the camera.
Apply closes the preview and requires a fresh F hold at a preparation station.
The character panel also arms book reading and City Charter activation.

I/Bag opens your carried inventory. Assign owned books/ward charges to 1–4;
hold a book shortcut to learn, or use it in the bag and make a fresh F hold.
Ward Use arms placement; confirm nearby clear ground, or cancel without cost.
Sorting preserves identity references. Drop requires explicit confirmation.
At the Guild, **Guild books** opens the live catalog and bag. Buy one uses the
shown quote and waits for the authority receipt; changed prices require review.
Unused charges are lost on death and none are granted on return. Select an observed nearby humanoid and press T or **Offer trade**. Both sides
edit their offers, press Ready, review both directions, then separately Confirm.
Gold stays exposed to ordinary theft and death; Cancel releases reserved items. Minimap left-click/drag pans; right-click cancels an armed action
first, then issues a ground move. Other selection shows Unknown private fields
while commands remain yours. Current economy gameplay includes eight paid jobs,
first visits, Witness checks, finite gathering/crafting, regional markets,
Treasury banking and bounded Supply/Industrial procurement. Earn Witness and
both distinct endorsements, pay100 gold at Treasury with an explicit wallet/bank
split, then activate the registered Charter at the City civic terminal with a
fresh four-second hold. Its30-second warning/60-second public opening permits
any eligible contender to win a ten-second Heart claim. Damage/movement and
nearby opponents still interrupt or contest in this v0.6 runtime. Old Seal
fabrication/private access is disabled in normal economy; new v0.7 siege,
keys/artifact routes and control-based claim rules remain pending.

Historical regression runners explicitly select `?Profile=V04`. An unqualified
legacy sandbox retains the [v0.3 profile](docs/v03/implementation/README.md).

## Setup

1. Install/associate Unreal **5.8.4 CL 58902052**. This patch baseline was approved
   by the user on 22 October 2026; preserve historical 5.8.3 evidence separately.
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
./Scripts/RunInputCheck.ps1 -Region -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 54
./Scripts/RunHUDCheck.ps1 -Region -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 49
./Scripts/RunInventoryCheck.ps1 -Region -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 49
./Scripts/RunNavigationCheck.ps1 -Region -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 54
./Scripts/CaptureInputUI.ps1 -Controls
./Scripts/CaptureInputUI.ps1 -Scene Selected -Width 1920 -Height 1080
./Scripts/CaptureInputUI.ps1 -Scene Selected -LargeText
./Scripts/CaptureInputUI.ps1 -Scene Inventory -Width 1920 -Height 1080
./Scripts/CaptureInputUI.ps1 -Scene DropConfirm -LargeText
./Scripts/RunDevelopmentCheck.ps1
./Scripts/RunDevelopmentCheck.ps1 -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -Reset
./Scripts/RunDevelopmentCheck.ps1 -Preparation
./Scripts/RunDevelopmentCheck.ps1 -Preparation -Normal
./Scripts/RunDevelopmentCheck.ps1 -Preparation -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks -Normal
./Scripts/RunDevelopmentCheck.ps1 -WorldChecks -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -Region
./Scripts/RunDevelopmentCheck.ps1 -Region -Alternate
./Scripts/RunDevelopmentCheck.ps1 -Region -Normal
./Scripts/RunDevelopmentCheck.ps1 -Region -Network -RoundTripMs 150 -JitterMs 25
./Scripts/RunDevelopmentCheck.ps1 -Region -Reset
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

The generator saves 28 packages: five maps, four data assets, eleven input actions, a
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
