# Project Lockdown v0.7 — tactical combat, progression and contested victory

Specification revision 1 • 24 October 2026

Build on the actual v0.6 Unreal project. This package is an implementation contract and validated document/data model, not a game build. No game repository, engine binaries or runtime evidence were provided for this task. Audit delivered behavior before changing it.

v0.7 implements tactical combat effects and MP, 28 new abilities, 40 equipment/tactical item definitions, natural-stat prerequisites, five shared active slots, three quick-item references, six equipment bindings, deterministic equipment assembly/quality, skill mastery, three guardian towers, rare POIs, and all four access routes to one ten-second Heart claim. Preserve working economic institutions, native movement, filtered observation, consent trade and source/sink ledgers.

## Read order

1. `00_DECISIONS_AND_MIGRATION.md`: precedence, explicit changes and deferred features.
2. `01_SCOPE_AND_RELEASE_PROFILE.md`: complete release scope and staged gates.
3. `02_COMBAT_RUNTIME_AND_EFFECTS.md` and `03_TARGETING_VISION_AND_TERRAIN.md`: normative mechanics.
4. `04_PROGRESSION_EQUIPMENT_AND_LOADOUTS.md`: build rules and formulas.
5. `05_ABILITY_CATALOG.md`, `06_ITEM_CATALOG.md`, `07_ACQUISITION_ASSEMBLY_AND_CRAFTING.md`: concrete content.
6. `08_WORLD_POIS_AND_NPC_POLICY.md`, `09_VICTORY_AND_MATCH_STATE.md`, `10_ECONOMY_DEATH_AND_ACCOUNTING.md`: integrated world.
7. `11_AUTHORITY_NETWORKING_AND_UI.md`: native implementation boundaries.
8. `12_BUILD_PLAN.md`, `13_ACCEPTANCE_AND_PLAYTEST.md`, `14_CODEX_BUILD_PROMPT.md`: developer execution.
9. `15_SOURCE_HISTORY_AND_LIMITS.md`, `MODEL_VALIDATION.md`: provenance and completed specification QA.

`16_DATA_CONTRACT_AND_IMPORT.md` defines operation semantics and import rules. `data/*.json` holds synchronized provisional tuning/catalogs. `tools/check_spec.py` checks reference integrity, reachability, accounting, recipe graphs, timing examples and required coverage. It is not an Unreal implementation. `templates` contains a feature ledger and implementation report. `reference-v06` is the frozen complete attached predecessor package (including v0.5 excerpts); `reference-discussion` contains the retrieved combat draft and a decision digest. Historical rules are not simultaneous active rules.

Normal v0.7 integration: 1–4 locked contenders, 35 minutes, level cap 15, 24 total carried instances, five active skills and three quick slots. The full design still targets 60–90 minutes; expected compressed level distribution must be measured rather than forced to cap. The separate v0.6 regression profile stays 25 minutes/level five/four skills/four quick references, without new content. Never mix these profiles in one match.

All 28 new abilities and 40 catalog definitions are release scope, delivered through milestones rather than all at once. A core combat milestone is useful but is not the complete v0.7 release. Generic art is acceptable if effects are readable. Normal progression, all four acquisition paths and independent-client evidence are required; human balance evidence may remain explicitly pending when unavailable.

Run document QA: `python3 tools/check_spec.py`. Then read the build prompt before implementing on real source.
