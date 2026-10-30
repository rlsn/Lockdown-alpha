# Project Lockdown — living game design

Revision 27 · 30 October 2026 · Long-term product and design reference

Lockdown is a competitive session RPG in a living world, viewed and controlled from above. Ordinary work, exploration, trade, theft and combat develop into a struggle for the Grand Heart. A class gives a starting identity; the player builds their capabilities during the match. Information, preparation and timing let a weaker duelist change the result of a stronger contender's plan.

The intended experience is **stealth developing into chaos**: establish a life among believable residents, discover opportunities and rivals, commit to a strategy, then decide when to reveal yourself and contest the final acquisition. The winner physically takes the Heart. Wealth, kills, survival and level are means of gaining opportunity, never substitute victory scores.

This directory explains the game across releases. Exact current rules belong to active release contracts; actual coverage belongs to implementation reports and evidence. The native baseline reviewed for this revision is **v0.8 B86, commit ebc19a5**. v0.9 adds the full-map contract and B90 spatial implementation; later gameplay and full-session gates remain open. See [delivery frontier](14_DELIVERY_FRONTIER_AND_OPEN_QUESTIONS.md) and [continuation](../CONTINUATION.md).

The [B90 world skeleton](../v09/B90_COMPLETION.md) establishes connected native
terrain, roads, rivers, settlements and permanent approaches. The
[B91 completion review](../v09/B91_COMPLETION.md) now brings those places into the
interaction system: interiors and doors, finite storage/cargo, portable tools,
optional roof links, mutable foliage, grass concealment, directional ledges,
paid environment reactions and 3,000 fixed scenery trunks. Its connected
Riverport journey, earned environment reload and final native regression matrix
pass. B91 is complete at its functional-environment boundary.

The [control decision](../v09/CONTROL_POLICY_OVERRIDE.md) governs every new object:
ordinary pointer interactions, real prepared abilities/items, normal attacks or
eligible automatic POIs. There is no generic world-interaction key. One
confirmation can approach and channel; S stops the order. World consequences
persist through a [settled checkpoint](../v09/B91_CHECKPOINT.md), while full
account/economy recovery belongs to later integration.

The next product layer is B92: make the environment support an actual life,
finite acquisition, local services and believable opposition. B93 connects
those earned preparations to victory; B94 tests duration, multiplayer stress,
readability and human pacing. A functioning environment alone does not establish
that the complete match is engaging or balanced.

B92's [economy authority foundation](../v09/B92_ECONOMY_FOUNDATION.md) now audits
the actual zero-money/empty-inventory starter and existing Witness operation,
and verifies five finite pools and funded institutional schedules. That report
isolates the authority boundary. Subsequent integration must connect services,
work, population behavior and earned progression to the same finite resources.

The [B92 roster review](../v09/B92_ROSTER.md) now connects 24 persistent seats,
196 role bodies and the economic identities. Control changes preserve a seat's
body and development; death/return retain its accounts and finite sources.
The [remote service increment](../v09/B92_SERVICES.md) connects commodity counters
to actual bags and finite cash, with one accounting domain for world/container
goods and market stock. Two remote owners and replacement-seat takeover are
verified. Role routines, security, autonomous progression, earned work/acquisition
and authenticated reconnect remain open. Body counts and isolated trading do
not establish a living-world experience.

The [physical banking increment](../v09/B92_BANK.md) extends the five counters with one persistent bank account. Deposits burn a rounded fee; withdrawals make savings carried and exposed again. Ordinary work, earned acquisition and useful build preparation remain the next economic dependencies.

The [official supply increment](../v09/B92_SUPPLY.md) connects paid catalog goods
and tiered books to authored supply/trainer/archive endpoints. Circle-III stock
belongs to the whole match, with two purchases per definition. Normal earnings,
learning and useful build acquisition still need connected progression evidence.

The [finite deposit increment](../v09/B92_DEPOSITS.md) adds automatic eligible
six-second extraction at nine resource POIs. Stop must remain effective while
standing there; leaving/re-entering restores automatic eligibility. All54 charges
join the canonical item ledger. Earned progression remains separate work.

The [prepared Survey increment](../v09/B92_SURVEY.md) now connects the inherited
one-second skill to Crystal deposits and their existing charges. The player
prepares and confirms a real skill; it is not a free interaction shortcut.
Earned preparation and the work/Witness/Charter journey remain open.

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
