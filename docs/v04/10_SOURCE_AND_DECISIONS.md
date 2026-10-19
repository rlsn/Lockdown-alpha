# Source interpretation and release decisions

## New source

Compare Attribute Systems, conversation 6ad49766-693c-83e8-82aa-9a0fc9b775f7, was inspected on 19 October 2026. All four available user turns and readable assistant material were reviewed. One assistant response is bounded at the reader's 20000-character cap; no missing tail is inferred.

The user first proposed six attributes using D&D-inspired examples, then explicitly relaxed a strict no-RNG position, proposed removing heroes, retained player allocation and skill scaling, and added class skills, possible subclasses and universal learning from books. The next user correction requires one active-slot pool for both utility and combat and extends the full target to 60–90 minutes. The final user turn allows flexible maps such as four outer cities, a centre, wilderness and towns, while retaining the Heart and towers.

Later user corrections control over earlier assistant suggestions. In particular, free utility outside combat slots, automatic hero growth and the old short full-match target are superseded. Eight slots, level twenty and particular class or city tables remain illustrative. The source's claim that three districts resulted from an attribute count is not established project history.

## Proposed v0.4 choices

| ID | Choice and reason |
| --- | --- |
| D40 | Three class starts, each 60 attribute points, to demonstrate different starting competencies |
| D41 | Five levels, cap 16 per attribute and modest bounded scaling, to test allocation without a full RPG curve |
| D42 | Four active slots and nine skills, so learned-versus-equipped decisions occur quickly |
| D43 | Six small subclass options at level four, to test commitment without a content tree |
| D44 | Nine named skills and four universal books, with rank 1–3 and explicit costs |
| D45 | Deterministic Charter tasks plus only one probabilistic skill, Pickpocket, to test the resolution split |
| D46 | One city, two towns and wilderness, with configurable graph data rather than a final full world |
| D47 | A 25-minute integration cap and a separate technical soak; no full 60–90 minute pacing claim |
| D48 | Real commissions and fabrication replace the v0.3 seeded Seal; full conquest remains deferred |
| D49 | A bounded v0.4 respawn formula and 15 percent gold loss, retaining build knowledge |
| D50 | Roster lock prevents fresh mid-match guests resetting budgets; keep verified authenticated resume if present |

All D decisions, coefficients, item prices, timings and names are development proposals. They are not presented as earlier explicit approvals or balanced results. If actual baseline code differs, record the difference and decide deliberately rather than silently overwriting it.

## Long-term boundary

Remote Heart acquisition is a source assistant proposal preserved in the master plan. v0.4 retains a central claim for the implemented route and does not validate that future alternative. Suspicion, broad inspections, true sight, class-exclusive trees, passive skills, level twenty, eight slots, four outer cities, factions, economy simulation and complete AI strategies remain future work.

## Official engineering references

Consulted 19 October 2026; use the installed engine's matching branch. The project does not require an upgrade to the branch displayed by the live site.

- [Epic Data Assets](https://dev.epicgames.com/documentation/unreal-engine/data-assets-in-unreal-engine) supports the catalog and asset identity approach.
- [Epic Gameplay Ability System](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system) describes optional ability infrastructure; it does not mandate a migration for this project.
- [Epic attribute replication](https://dev.epicgames.com/documentation/unreal-engine/gameplay-attributes-and-attribute-sets-for-the-gameplay-ability-system-in-unreal-engine) shows that attributes need deliberate replication policy rather than assuming privacy.
- [Epic random streams](https://dev.epicgames.com/documentation/en-us/unreal-engine/random-streams-in-unreal-engine) describes repeatable seeded sequences; retry limits, seed secrecy and atomic outcomes are additional project rules.

No external game rules, named heroes, spell lists or balance coefficients are copied as authoritative requirements. The application repository and actual Unreal results were not supplied during this documentation task.
