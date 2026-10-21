# Project Lockdown master plan

Revision 5 • 21 October 2026 • Long term reference

Project Lockdown is a competitive session RPG in a living world. Characters begin with a class and develop their own attributes, skills, profession and specialization. Combat, work, knowledge, theft, exploration and influence can support competing approaches to the Grand Heart. Appearance provides incomplete information about capability.

The full-match target is now **60–90 minutes**. There are **no predefined heroes**, no assigned primary attribute and no automatic class-specific attribute growth. Learned combat and utility actives compete for **one shared equipped-slot budget**. Four outer cities and a central city are a candidate layout, not a fixed world rule.

## Contents

| Document | Purpose |
| --- | --- |
| [01_VISION_AND_PLAYER_EXPERIENCE.md](01_VISION_AND_PLAYER_EXPERIENCE.md) | Experience and player decisions |
| [02_CITY_AND_NPC_SYSTEMS.md](02_CITY_AND_NPC_SYSTEMS.md) | Population roles and shared character rules |
| [03_WORLD_VISION_AND_POIS.md](03_WORLD_VISION_AND_POIS.md) | Regional geography, surveillance and objectives |
| [04_COMBAT_CHARACTERS_AND_GROWTH.md](04_COMBAT_CHARACTERS_AND_GROWTH.md) | Classes, attributes, skills, books and specialization |
| [05_GRAND_HEART_AND_MATCH_RULES.md](05_GRAND_HEART_AND_MATCH_RULES.md) | Acquisition routes, death and endings |
| [06_ROADMAP_AND_PROJECT_GOVERNANCE.md](06_ROADMAP_AND_PROJECT_GOVERNANCE.md) | Delivery, risks and evidence |
| [07_DECISIONS_AND_SOURCE_HISTORY.md](07_DECISIONS_AND_SOURCE_HISTORY.md) | Source authority and superseded assumptions |
| [08_PLATFORM_AND_MIGRATION.md](08_PLATFORM_AND_MIGRATION.md) | Unreal foundation and migration |
| [09_CHECKS_AND_INFORMATION.md](09_CHECKS_AND_INFORMATION.md) | Resolution policies and hidden builds |
| [10_SESSION_PACING_AND_WORLD_SCALE.md](10_SESSION_PACING_AND_WORLD_SCALE.md) | Long sessions and configurable maps |
| [11_UI_CONTROLS_AND_INVENTORY.md](11_UI_CONTROLS_AND_INVENTORY.md) | Navigation, selection, HUD, inventory and trade |

## Authority

Direct user decisions take precedence. Revision 5 retains the character direction and adds a consistent interface and right-click navigation. Revision 4 replaced hero-roster plans, the 20–30 minute full-match target, fixed three-district assumptions and separate combat versus utility slot budgets. Earlier numerical attribute bonuses were brainstorming examples, not approved coefficients.

The source's eight-slot example, level-20 cap and city themes remain proposals. The v0.4 specification selected a smaller implementation target; v0.5 retains that reduced gameplay profile while improving UI and control. New sprint tuning is explicitly proposed. A short prototype does not validate the full 60–90 minute experience.

The user has moved development to the v0.5 UI and control sprint. This repository contains the actual Unreal implementation and [v0.4 delivery evidence](../v04/B47_IMPLEMENTATION.md). The [v0.5 baseline review](../v05/B50_BASELINE_REVIEW.md) records the inspected revision, a fresh packaged smoke check and retained deviations. Human acceptance and deferred stress validation remain separate. Prior master revisions are preserved in Git history.

The interface direction combines a minimap and lower character/skill panel with a large inventory and a smaller quick-item bar. Right-click path navigation replaces WASD as the default movement scheme. Selecting another unit changes the inspector, never control ownership or private-information authorization. Exact v0.5 capacities and bindings are proposed release defaults.
