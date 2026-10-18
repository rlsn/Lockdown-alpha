# Unreal project scaffold contract

## Required repository layout

Use this proposed layout, adapting names only where an existing valid Unreal project requires it. The tree describes deliverables for Codex to create; this specification package does not contain those generated binaries.

```text
ProjectLockdown.uproject
Source/
  ProjectLockdown.Target.cs
  ProjectLockdownEditor.Target.cs
  ProjectLockdownServer.Target.cs        conditional dedicated build lane
  ProjectLockdown/
    ProjectLockdown.Build.cs
    Public/ and Private/
      Core/  Framework/  Character/  Interaction/  World/  Vision/  UI/  Tests/
  ProjectLockdownEditor/
    ProjectLockdownEditor.Build.cs
    Private/ScaffoldBootstrap/ and Validation/
Config/
  DefaultEngine.ini  DefaultGame.ini  DefaultInput.ini  PlatformLock.json
Content/ProjectLockdown/
  Maps/  Characters/  Materials/  Input/  UI/  Data/  World/  Tests/
Scripts/
Docs/
  EnvironmentAudit.md  EngineVersion.md  Architecture.md  MigrationLedger.csv
  AssetManifest.csv  TestReport.md  KnownLimitations.md  DemoGuide.md
```

Separate runtime and editor code. Runtime modules must not depend on `UnrealEd` or call editor package-saving APIs. Start with ordinary engine framework modules, Enhanced Input, UMG and navigation only where needed. Add dependencies deliberately. Do not start with GAS, CommonUI, Game Features, Mass, an online subsystem or a marketplace framework merely because it is available.

## Framework responsibilities

The following are project design choices, informed by Unreal's framework roles. Use the selected engine version's APIs and record any change to the split. [Epic framework reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine).

| Class or service | Responsibility | Information boundary |
| --- | --- | --- |
| `ALDGameMode` | Server match lifecycle, admission, spawn, death, return and terminal claim | Server only |
| `ALDGameState` | Explicit public phase, time and objective facts | No private actor, inventory or role registry |
| `ALDPlayerController` | Local input, camera, UI and validated server requests | Own connection; never a remote data browser |
| `ALDPlayerState` | Contender identity and retained private progression | Do not expose enemy body or proxy identity links |
| `ALDCharacter` | Authoritative or owner-controlled body with Character Movement | Remote clients do not receive this canonical body directly |
| `ULDCharacterMovementComponent` | Sprint policy and compatible dash integration | Preserve native movement prediction and server correction |
| `ULDLifeComponent` | HP, active life generation, death and protection | Owner state and server rules; sanitized observed cues only |
| `ULDCombatComponent` | Attack legality, commitments, impact and interruption | Server damage; owner acknowledgement |
| `ULDInteractionComponent` | Candidate IDs, channel requests and cancellation | Recheck visibility, distance, life and ownership |
| `ULDVisionWorldSubsystem` | Server source union, geometry tests and observation builder | Canonical actors never handed directly to UI |
| `ALDWorldViewRelay` | One owner-only filtered world view per connection | Opaque observed IDs and permitted fields only |
| `ALDObservedEntityProxy` | Client-only render representation of another body | Same proxy schema for resident, bot and human |
| `ULDUIRoot` and focused widgets | HUD, prompts, panels, help and result state | View models; no authoritative writes |

Keep pure rule helpers for gold loss, respawn duration, access predicates, claim completion and unit conversion. This lets tests check semantics without spawning a full level. Use project data assets or a validated configuration struct for tuning, rather than magic values in Blueprint graphs.

## Match and life states

Use `Frontend -> Loading -> ScaffoldPlaying -> Results`, with a fresh match generation on restart. Within a contender, use `Active`, `WaitingRespawn` and `Withdrawn`. Replacing the pawn must not reset retained gold or profession state. A terminal claim must be committed once. No default template elimination mode, kill score or remaining-player callback may award a win.

Create stable server contender IDs and fresh life IDs. Keep observed world IDs opaque and unrelated to engine network GUIDs, player IDs or role categories. GameState, PlayerState and lobby information must not create a link from a public identity to an otherwise neutral observed body.

Native direct-address joining does not reproduce the old room-code service. A local host is one game session. Indicate that limitation in the ledger and UI. Guest names may appear in connection or results screens, but must not label remote crowd bodies during play.

The scaffold does not promise resumable guest sessions across network disconnects. On confirmed disconnect or leave, withdraw the contender, cancel its holds, drop its carried Seal once and remove its body or pending return. A later direct join creates a new contender. Mark v0.2's ten-second authenticated resume window Deferred in the ledger; do not silently equate native reconnect with the old guest-token system. Death and respawn within a connected session must retain the contender correctly.

## Data and interfaces

Create one `ULDPrototypeTuning` asset or equivalent authoritative data record with schema version, source baseline revision and converted values. Include body dimensions, speeds, action timings, sight radii, protection, respawn formula, interaction distances, courier reward, ward lifetime and capture duration. Validate it at startup; reject missing or invalid required values with useful diagnostics.

Use small interfaces for interactable objects, vision sources and observed render state. Planned items, towers and profession systems need extension points, but avoid empty managers for every future subsystem. A declared interface must have at least one exercised implementation in the slice or be explicitly marked deferred.

Preserve exact integer or fixed-point arithmetic for currency. A delivery receipt, death receipt and item transfer must be idempotent. Do not use a Blueprint animation callback as the authority for receiving gold or winning the match.

## Content that must exist

| Asset or map | Required behavior |
| --- | --- |
| `L_Frontend` | Native Host migration demo, Join address, controls, connection feedback and Exit |
| `L_MigrationSandbox` | Navigable public and covered routes, height test, small crowd, combat space, one courier loop and a Heart area |
| Input assets | Move, sprint, aim, attack, dash, assassination, interact, ward, help and menu contexts with release and cancellation handling |
| Character presentation | One coherent humanoid placeholder and shared locomotion, facing, commitment and damage cues |
| UI assets | Root HUD, interaction prompt, channel bar, owner status, respawn and results widgets |
| Fog material and mask binding | Visible, explored and unseen terrain with dynamic entity removal independent of shading |
| Tuning and scenario data | Deterministic identifiers, source units, converted positions and explicit fixture profile |
| Functional test area or map | Repeatable ray, height, movement, source union, interruption and ownership scenarios |

Use real assets referenced through valid paths. Ensure required maps and soft-referenced data are included in cooking. A successful editor preview with missing packaged assets fails the scaffold gate.

## Bootstrap contract

Implement an editor-only generator or repeatable editor utility driven by committed data. It creates the two maps, default meshes and materials, input assets, required data and test placement. It validates package saves and returns a failing result on missing assets. Include a generation manifest and version marker.

Rerunning in validate mode must report differences without overwriting hand edits. An explicit regenerate mode can update generated assets under their declared directory only. This is a small asset pipeline, not a generic level editor. Never include a system-wide cleanup command in a project bootstrap.

## Debug and fixtures

The sandbox is visibly labelled Unreal migration demo. Developer controls can seed a Seal, set the four defensive flags or reset a scenario. They are permitted only in the named scaffold profile, run on authority, are logged, and cannot be invoked by an unprivileged remote client. Real item pickup, drop, claim, delivery and combat then use ordinary validated paths.

Debug overlays may show coordinates or rays on the server, but hidden canonical data must never enter a restricted client's relay. Local test instrumentation is separate from shipping data. Console access or PIE convenience is not a replacement for a usable Host and Join interface.
