# Project Lockdown — living game design

Revision 9 · 28 October 2026 · Long-term product and design reference

Lockdown is a competitive session RPG in a living world, viewed and controlled from above. Ordinary work, exploration, trade, theft and combat develop into a struggle for the Grand Heart. A class gives a starting identity; the player builds their capabilities during the match. Information, preparation and timing let a weaker duelist change the result of a stronger contender's plan.

The intended experience is **stealth developing into chaos**: establish a life among believable residents, discover opportunities and rivals, commit to a strategy, then decide when to reveal yourself and contest the final acquisition. The winner physically takes the Heart. Wealth, kills, survival and level are means of gaining opportunity, never substitute victory scores.

This directory explains the game across releases. Exact current rules belong to active release contracts; actual coverage belongs to implementation reports and evidence. The native baseline reviewed for this revision is **v0.8 B86, commit ebc19a5**. v0.9 adds the full-map contract and B90 spatial implementation; later gameplay and full-session gates remain open. See [delivery frontier](14_DELIVERY_FRONTIER_AND_OPEN_QUESTIONS.md) and [continuation](../CONTINUATION.md).

The [B90 world skeleton is complete](../v09/B90_COMPLETION.md) at its spatial
boundary: native journeys, movement boundaries, river/ice contact, key-target
approaches, settlement/service mapping, doorway clearance, deterministic saved
geometry and actual reset/profile isolation. F6 exposes public settlement
geography and policy outlines; live services and guards remain B92 work. The
current implementation stage is [B91's functional environment](../v09/B91_IMPLEMENTATION.md),
with structural interiors, permanent stairs and native door authority verified.
The [storage/cargo checkpoint](../v09/B91_OBJECTS.md) adds finite ownership and
crate/barrel transport. [Portable tools](../v09/B91_TOOLS.md) now retain fuel and
provide cosmetic light. [Approved ladder/rope links](../v09/B91_TRAVERSAL.md)
add shared optional vertical routes with safe interrupted traversal. Socket fire,
foliage, full-map fields and density remain. Living population,
full-map economy and earned victory integration remain on the delivery frontier.

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
| Control | Right-click navigation, explicit attack, held context interactions, remappable bindings |
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
