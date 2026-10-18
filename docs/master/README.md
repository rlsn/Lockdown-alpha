# Project Lockdown master plan

Revision 3 • 18 October 2026 • Long term reference

Project Lockdown is an online game about pursuing the Grand Heart through a living city. Players can grow through peaceful work, risky exploration or violence. Crowds conceal intent, surveillance creates safer public spaces, and the loss of towers transforms the map. Death costs time and gold; obtaining the Grand Heart decides the winner.

This package is the current long term design authority. It supersedes the earlier last survivor direction and its permanent elimination and shrinking circle assumptions. Release specifications select a manageable part of this vision. They do not make every future feature an immediate development task.

## Contents

| Document | Reference purpose |
| --- | --- |
| [01_VISION_AND_PLAYER_EXPERIENCE.md](01_VISION_AND_PLAYER_EXPERIENCE.md) | Premise, audience hypothesis, match experience and design principles |
| [02_CITY_AND_NPC_SYSTEMS.md](02_CITY_AND_NPC_SYSTEMS.md) | Peaceful population, autonomous contenders, wild threats and world activity |
| [03_WORLD_VISION_AND_POIS.md](03_WORLD_VISION_AND_POIS.md) | Fog, terrain, wards, surveillance and the POI ecosystem |
| [04_COMBAT_CHARACTERS_AND_GROWTH.md](04_COMBAT_CHARACTERS_AND_GROWTH.md) | Combat direction, character and stat policy, professions, items and rewards |
| [05_GRAND_HEART_AND_MATCH_RULES.md](05_GRAND_HEART_AND_MATCH_RULES.md) | Victory routes, tower progression, death, respawning and end conditions |
| [06_ROADMAP_AND_PROJECT_GOVERNANCE.md](06_ROADMAP_AND_PROJECT_GOVERNANCE.md) | Release sequence, evidence, risks, ownership and change control |
| [07_DECISIONS_AND_SOURCE_HISTORY.md](07_DECISIONS_AND_SOURCE_HISTORY.md) | User directions, proposed defaults, superseded decisions and source limits |
| [08_PLATFORM_AND_MIGRATION.md](08_PLATFORM_AND_MIGRATION.md) | Unreal choice, 3D presentation, native delivery and the scaffold milestone |

## How to use the plan

Use this package to assess whether a feature advances the intended experience. The current development milestone is the v0.3 Unreal migration and scaffold package. v0.2 remains the gameplay reference and comparison prototype. When an implementation detail conflicts with a release specification, record and resolve that conflict before changing behavior. A later direct instruction from the project owner takes precedence over both.

Statements labelled **Direction** summarize the user's expressed design direction. **Proposed** identifies choices made to turn that direction into a testable system. **Deferred** identifies future work without authorizing implementation. Numerical release values are proposed tuning, not established balance or playtest findings.

The user now reports v0.2 complete and promising, while its graphics and UX prevent serious testing. Unreal and 3D or 2.5D presentation are the selected next direction. No application repository or engine installation was available in this documentation workspace, so implementation and toolchain verification remain required. Revision 3 updates the platform milestone; it does not claim an independently verified build.
