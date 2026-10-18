# Project Lockdown v0 3 Unreal migration package

Specification revision 1 • 18 October 2026 • Target platform Windows desktop

Move Project Lockdown from its web prototype to an Unreal Engine C++ foundation with a three dimensional world and top down 2.5D play. Preserve the v0.2 game direction while proving a repeatable editor and packaged-build workflow, readable movement and interaction, native multiplayer authority, and fog that actually limits information.

This release is a platform migration and scaffold, not a complete v0.2 feature port or a new gameplay expansion. A small working slice and a precise migration ledger are required. An empty project, a collection of uncompiled classes, or a visual scene without authoritative networking is insufficient.

## Source and current status

In **Compare Web And Unity**, the user reports that v0.2 is complete and promising, but its 2D graphics and UX prevent serious testing. The user asks for 3D or 2.5D and selects Unreal after comparing pricing models. This package accepts that platform choice; it does not reopen the engine comparison or make licensing claims.

All four available user turns were read. The chat reader returned assistant replies as content references, so no unseen setup instructions or exact engine version are treated as approved. The v0.2 documents were reviewed; its application repository and the installed Unreal toolchain were not supplied here. Codex must inspect the actual build before migration.

## Reading order

| File | Purpose |
| --- | --- |
| [01_SCOPE_AND_MIGRATION.md](01_SCOPE_AND_MIGRATION.md) | What to retain, replace, demonstrate and defer |
| [02_UNREAL_SETUP_AND_WORKFLOW.md](02_UNREAL_SETUP_AND_WORKFLOW.md) | Engine and compiler audit, repeatable commands, binary asset workflow and build lanes |
| [03_PROJECT_SCAFFOLD.md](03_PROJECT_SCAFFOLD.md) | Required project tree, classes, configuration, content and ownership |
| [04_WORLD_CAMERA_AND_UX.md](04_WORLD_CAMERA_AND_UX.md) | Unit conversion, terrain, camera, controls and presentation gates |
| [05_MULTIPLAYER_AND_VISION.md](05_MULTIPLAYER_AND_VISION.md) | Native authority, movement, private world views and sight tests |
| [06_PORT_SLICE_AND_BUILD_PLAN.md](06_PORT_SLICE_AND_BUILD_PLAN.md) | Minimal working slice and dependency ordered development tasks |
| [07_ACCEPTANCE_AND_DELIVERY.md](07_ACCEPTANCE_AND_DELIVERY.md) | Acceptance checks, testing conditions and delivery evidence |
| [08_CODEX_BUILD_PROMPT.md](08_CODEX_BUILD_PROMPT.md) | Ready-to-paste development request |
| [09_SOURCES_AND_DECISIONS.md](09_SOURCES_AND_DECISIONS.md) | Source limits, proposed decisions and official technical references |
| [templates/MIGRATION_LEDGER.csv](templates/MIGRATION_LEDGER.csv) | Feature inventory template to fill from the actual v0.2 implementation |
| [templates/IMPLEMENTATION_REPORT.md](templates/IMPLEMENTATION_REPORT.md) | Honest completion and verification report template |
| [reference-v02/README.md](reference-v02/README.md) | Frozen gameplay, world, engineering and test reference |

## Authority

The user's current directions take precedence. This package replaces v0.2's web runtime, rendering, movement transport, browser UI and deployment assumptions. It retains the Grand Heart, multiple access routes, costly respawns, peaceful growth, crowd concealment and medium lethality as product intent. The frozen v0.2 engineering contract is historical where it conflicts with Unreal integration.

All new architecture, numerical scale choices and scaffold boundaries are proposed implementation defaults. They are not claimed as prior explicit approvals. Within this package, scope and slice coverage decide what must be built now; deferred v0.2 features do not become requirements merely because their old specifications are bundled.

## Required result

A pinned Unreal C++ project opens and cold-builds, generates or contains valid assets, packages a Windows Development game, and runs two independently controlled clients. Its migration-demo map demonstrates top down movement on actual 3D terrain, combat and death feedback, a small civilian crowd, one paid courier delivery, a ten second Heart claim, a private ward, shared tower sight and low versus high-ground visibility.

Demo access to Seals and tower states may use clearly labelled developer fixtures. That proves the new platform contracts, not full peaceful progression or assault balance. A migration ledger must distinguish implemented mechanics from fixtures and deferred ports. Preserve the working web prototype as the comparison baseline.

Send this folder, the current master plan and the real v0.2 repository to Codex. Paste the included build prompt. This package contains specifications and templates only; it is not a generated Unreal project or a verified engine installation.
