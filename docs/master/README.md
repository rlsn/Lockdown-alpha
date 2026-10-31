# Project Lockdown — living game design

Revision 31 · 31 October 2026 · Long-term product and design reference

Lockdown is a competitive session RPG in a living world, viewed and controlled from above. Ordinary work, exploration, trade, theft and combat develop into a struggle for the Grand Heart. A class gives a starting identity; the player builds their capabilities during the match. Information, preparation and timing let a weaker duelist change the result of a stronger contender's plan.

The intended experience is **stealth developing into chaos**: establish a life among believable residents, discover opportunities and rivals, commit to a strategy, then decide when to reveal yourself and contest the final acquisition. The winner physically takes the Heart. Wealth, kills, survival and level are means of gaining opportunity, never substitute victory scores.

This directory explains the game across releases. Exact current rules belong to active release contracts; actual coverage belongs to implementation reports and evidence. The inherited mechanics baseline is **v0.8 B86, commit ebc19a5**. Current v0.9 delivery covers the B90 world, B91 environment and incremental B92 economy/progression; population behavior, route convergence and full-session gates remain open. See [delivery frontier](14_DELIVERY_FRONTIER_AND_OPEN_QUESTIONS.md) and [continuation](../CONTINUATION.md).

## Core loop and commitments

Find a local opportunity, acquire knowledge or physical goods, prepare a limited toolkit, then expose that investment in pursuit of a larger opportunity. Work and trade make equipment possible; equipment changes which work, routes and opponents are worth confronting. Observation lets rivals respond without receiving private builds or inventories. Death retains development while risking exposed property, carried wealth, position and time.

The loop should branch. A player can sell a material, keep it for a transform, invest in a counter, bank money or advance an objective. The game loses depth when one safe repetitive job dominates these choices. Every system should explain what it costs, what persists, what an opponent can perceive and how that opponent can respond.

Preparation and execution are separate decisions. Knowing an ability does not place it on the action bar. Owning an item does not grant immediate backpack access. Quiet preparation at an authored station commits shared action/binding capacity; execution uses that real source with its ordinary timing and limits.

## Interaction language

Right-click handles movement, ordinary pickup and doors. Left-click opens storage, traders and service panels. Specialized world actions use a prepared ability or actual item; eligible automatic POIs use stationary presence. Never add a world-interaction key or a free generic interaction ability. Confirm a channel once, show its progress, continue after release and stop with S or an authoritative interruption. See the [control decision](../v09/CONTROL_POLICY_OVERRIDE.md) and [design principles](15_DESIGN_PRINCIPLES.md).

## Current delivery boundary

| Layer | Established boundary | Still to establish |
| --- | --- | --- |
| B90 spatial world | Connected terrain, routes, settlements and ordinary approaches | Retain affected-route regressions |
| B91 functional environment | Doors/interiors, finite storage/cargo/tools, foliage/grass, ledges, paid reactions and settled environmental checkpoints | Full active-match account/economy recovery belongs to later integration |
| B92 population and economy | Persistent seats and role bodies; finite physical services, bank, supply, deposits, work and courier lifecycle; earned Witness and registered Charter transaction route | Natural travel/teaching/pacing, local security/hostile behavior, routine readability, legal autonomous contenders, broad race/admission/recovery stress |
| B93 convergence | Selected four-route design | Full-map activation, warnings/access and the physical Heart ending |
| B94 session acceptance | Defined evaluation tasks | Sustained multiplayer, recorded performance, human readability and 60–90-minute pacing |

The [earned Charter review](../v09/B92_CHARTER.md) connects six normal courier channels, actual paid book reading, legal preparation, real crafting/gathering and both procurement proofs to a registered purchase for 100 gold. Travel and protection/quiet setup are staged; three alternative Witness builds are explicit fixtures. It establishes an earned transaction path without establishing a normal session or useful build diversity. Registration does not activate an opening or win the game. B92 remains in progress and `v09_full` remains closed.

The [routine increment](../v09/B92_ROUTINES.md) connects the existing civilian and traveler bodies to native walking, staggered activity and observed flee/resume. Sealed traveler packs are visible logistics scenery, with no loot or market stock. Guard/security, hostile behavior, autonomous contender policy and simulation tiers remain separate dependencies. A moving crowd alone does not establish a believable, sustainable match.

Keep implementation chronology, exact test counts and source hashes in the [delivery frontier](14_DELIVERY_FRONTIER_AND_OPEN_QUESTIONS.md), release reports and [continuation](../CONTINUATION.md). This directory supplies the lasting product model and decision criteria.

## Reading paths

| Need | Read |
| --- | --- |
| Understand the experience | [Vision](01_VISION_AND_PLAYER_EXPERIENCE.md), [principles](15_DESIGN_PRINCIPLES.md), [player journeys](16_PLAYER_JOURNEYS_AND_PLAYTESTS.md) |
| Follow a normal match | [Pacing](10_SESSION_PACING_AND_WORLD_SCALE.md), [growth/combat](04_COMBAT_CHARACTERS_AND_GROWTH.md), [Heart/recovery](05_GRAND_HEART_AND_MATCH_RULES.md) |
| Design the world | [Map/sight](03_WORLD_VISION_AND_POIS.md), [population/security](02_CITY_AND_NPC_SYSTEMS.md), [economy](12_ECONOMY_PRODUCTION_AND_INSTITUTIONS.md) |
| Design an interaction | [Controls/UI](11_UI_CONTROLS_AND_INVENTORY.md), [effects/information](09_CHECKS_AND_INFORMATION.md), [acquisition](13_ACQUISITION_AND_BUILD_DIVERSITY.md) |
| Implement a release | [Architecture](08_PLATFORM_AND_MIGRATION.md), [governance](06_ROADMAP_AND_PROJECT_GOVERNANCE.md), [decisions/history](07_DECISIONS_AND_SOURCE_HISTORY.md), [frontier](14_DELIVERY_FRONTIER_AND_OPEN_QUESTIONS.md) |

## Product at a glance

| Dimension | Direction |
| --- | --- |
| Format | One bounded competitive match; 60–90-minute full-session target with legal earlier wins |
| Perspective | Native Windows Unreal game, readable 3D world, controlled top-down camera |
| Control | Right-click pickup/doors/movement; left-click storage/traders; source actions and automatic POIs; one-press channels |
| Character | Six attributes, nine current starting presets, one intrinsic trait, cross-class learning |
| Preparation | Six shared actions, seven body equipment bindings, four accessory bindings, one capstone |
| World | Fixed major geography and deterministic detail; five cities, six towns, connected wilderness |
| Opposition | Human and autonomous contenders among residents, travelers, officials and threats |
| Information | Private builds/wallets; geometry-authorized observation; public objective events |
| Victory | Siege, public Charter, three keys or Heartbreaker authorize one physical ten-second claim |
| Loss | Development and ordinary equipped gear retained; exposed property, wallet and time remain at risk |
| Persistence | Match-local progression; no assumed persistent power between matches |

## Maintaining this reference

Update the relevant chapter when a decision changes its player-facing meaning. Keep numeric catalogs, acceptance rows and logs in release directories. A new mechanic must explain its decision, cost, response, information boundary, death/reset behavior and evaluation. A proposed idea stays visibly proposed until selected.

Implementation precedence: user decisions → explicit v0.9 full-profile overrides → unchanged v0.8 behavior → inherited v0.7/v0.6 contracts. Master intent guides choices where those leave room. Historical revision 7 remains in the [v0.8 source archive](../v08/reference/Project_Lockdown_Master_R7_Source.zip) and [v0.7 reference copy](../v07/reference-master-r7/README.md).
