# Project Lockdown v0.4 character development package

Specification revision 1 • 19 October 2026 • Extends the Unreal v0.3 foundation

**Repository integration:** this is the active release specification. Start with
the [current handoff](../CONTINUATION.md), [baseline review](BASELINE_REVIEW.md)
and [working feature ledger](FeatureLedger.csv) for actual repository status.
The supplied specifications and blank templates below are design inputs.
[B40 baseline verification](B40_BASELINE_AUDIT.md) and the B41 character/lobby
engineering checks have passed. The [B42 execution checkpoint](B42_IMPLEMENTATION.md)
adds equipped skill execution and the mixed hotbar. The [B43 checkpoint](B43_IMPLEMENTATION.md)
adds private preparation, branching and books. See the [implementation record](IMPLEMENTATION_REPORT.md)
for actual scope and evidence. The complete v0.4 release remains in development.

Build the next playable step around classes, six attributes, learned skills and a single combat-and-utility loadout. Replace the fixed shared kit with player-directed development, add cross-class books and contrasting world interactions, and make the peaceful Seal route actually earnable without a diagnostic grant.

This is a proposed implementation baseline, not a claim that v0.3 is complete. Audit the actual Unreal repository, engine lock, networking, fog and test results first. Retain working foundations. Close prerequisite gaps explicitly rather than presenting new documents as an existing build.

## Reading order

| File | Purpose |
| --- | --- |
| [01_SCOPE_AND_MIGRATION.md](01_SCOPE_AND_MIGRATION.md) | Release boundary and superseded assumptions |
| [02_ATTRIBUTES_AND_PROGRESSION.md](02_ATTRIBUTES_AND_PROGRESSION.md) | Classes, six attributes, levels, allocation and subclasses |
| [03_SKILLS_BOOKS_AND_LOADOUTS.md](03_SKILLS_BOOKS_AND_LOADOUTS.md) | Nine skills, learning, proficiency and unified slots |
| [04_CHECKS_THEFT_AND_TRANSACTIONS.md](04_CHECKS_THEFT_AND_TRANSACTIONS.md) | Thresholds, the theft roll and consequences |
| [05_WORLD_AND_OBJECTIVE_LOOP.md](05_WORLD_AND_OBJECTIVE_LOOP.md) | Small regional map, real work and earned acquisition |
| [06_UNREAL_AUTHORITY_AND_DATA.md](06_UNREAL_AUTHORITY_AND_DATA.md) | Architecture, privacy and transaction order |
| [07_BUILD_PLAN.md](07_BUILD_PLAN.md) | Ordered implementation work |
| [08_ACCEPTANCE_AND_PLAYTEST.md](08_ACCEPTANCE_AND_PLAYTEST.md) | Functional checks and human review |
| [09_CODEX_BUILD_PROMPT.md](09_CODEX_BUILD_PROMPT.md) | Ready-to-paste development request |
| [10_SOURCE_AND_DECISIONS.md](10_SOURCE_AND_DECISIONS.md) | Source interpretation and proposed defaults |
| [data/prototype_rules.json](data/prototype_rules.json) | Machine-readable tuning and catalog companion |
| [templates/FEATURE_LEDGER.csv](templates/FEATURE_LEDGER.csv) | Actual baseline and implementation status template |
| [templates/IMPLEMENTATION_REPORT.md](templates/IMPLEMENTATION_REPORT.md) | Delivery, evidence and limitations template |
| [reference-v03/README.md](reference-v03/README.md) | Frozen predecessor references |

## Target and scope

The full vision now targets 60–90 minute sessions, no predefined heroes, flexible regional maps and one active-slot budget across every learned active. v0.4 deliberately tests a smaller profile: three class starts, five character levels, four shared active slots, nine skills, six subclass choices, one city, two towns and a 25 minute cap. These counts and all new formulas are proposals.

The normal loop supports 1–4 humans, with one labelled practice and at least two independently controlled humans required for multiplayer evidence. Keep a small nonviolent crowd, one bounded wild encounter, public sight and private wards. Full four-outer-city geography, level twenty, eight slots, factions, procedural worlds, broad AI strategies and 60–90 minute human pacing are not claimed by this release.

## Success demonstration

Two characters with the same start make different allocations and preparations. A character learns a book skill from outside its class. More than four learned actives force an equip choice; utility cannot bypass the four slots. Combat and world checks use the real attributes and rank. Pickpocket resolves one server roll and transfers existing gold without replay or retry exploits. A zero-kill character earns commissions, satisfies a learned-skill witness, crafts a Seal and completes the real Heart claim while central defences remain.

## Authority and conflict handling

The user's current decisions and revision 4 master plan control intent. This package overrides v0.3's fixed kit, growth, keybindings, short timer and fixture-acquisition defaults. It does not undo Unreal authority, native movement, fog or private observation. The prose specifications control behavior; the JSON is a checked companion. Resolve any disagreement before implementation and update both together.

No code, compiled assets or game test results are delivered by this specification package. Send it, the current master plan and the actual Unreal repository to Codex with the included prompt.
