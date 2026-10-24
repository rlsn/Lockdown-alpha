# Decisions, precedence and migration

## Authority

Explicit user choices govern: tactical skills/items should swing encounters despite a level disadvantage; six freely developed attributes; class starts rather than fixed heroes; common combat/utility skill slots; hybrid economy and light production; economic unlock followed by contest; right-click native navigation; Grand Heart as the sole ordinary win. The user authorized an implementation package covering the recent combat/acquisition discussion. Detailed numbers below are newly selected prototype defaults, not retrospectively approved permanent rules.

Use this v0.7 package over conflicting v0.6 clauses for the v0.7 profile only. Use master revision 7 for long-term intent. Frozen predecessor and draft files are historical sources. Existing delivered behavior not explicitly superseded stays; a discrepancy is recorded and reconciled in prose/data/assets before release. Never claim a feature absent solely because source is not included here.

## Explicit changes

| Topic | Previous baseline / conflicting proposal | v0.7 disposition |
| --- | --- | --- |
| Charter | v0.6 public window; combat reply suggests private access | Keep registered PUBLIC Charter and all v0.6 endorsements/Witness/payment/recovery rules. No private Charter flag |
| Other access | Deferred siege; keys/artifact proposed | Implement permanent global siege opening and personal keys/artifact leases; all end in physical claim |
| Heart interruption | v0.6 ordinary damage/movement/proximity contest | Damage and mere enemy proximity no longer cancel; actual control, displacement and explicit interrupts matter. Voluntary movement out of claim cancels |
| Skills / items | Four skills, four quick references | Five common skills (Q/W/E/R/D), three quick references (1/2/3); no free utility bar |
| Mana | Baseline may have no MP | Add real HP/MP costs/regeneration for new skills. Retain actual legacy economic skill costs via audited adapters |
| Attributes | v0.6 weight expects 8–16; combat draft starts at five | Use 8–16 natural range, equal 53-point new presets (8 each +3/+2), one allocated point per level. Do not apply the five-point draft baseline to existing weight/check formulas |
| Level / duration | Five / 25-minute prototype | Cap 15 / 35-minute integration; full 60–90 remains future pacing validation |
| Gear / death | No required new gear, drop carried goods | Add six bindings; equipped ordinary gear retains, unequipped gear/books/consumables/cargo drop; designated objective gear always drops |
| Preparation | Baseline station preparation | Skills require station; equipment/quick references may change outside combat through a two-second commit, never instantly during fights |
| Supplies | Essentials availability not resolved | Official fixed-price unlimited baseline counters; paid creation and gold burn, no buyback |
| Assembly | Proposal includes portable kits / recipe discovery | Public deterministic workshop recipes, no portable kit, no ordinary recipe-discovery gate |
| Craftsmanship | Numeric draft +7/+14 percent | Adopt only physical weapon damage, flat armor and item barrier base; no attributes/control/cooldown growth |
| Anti-control | Optional DR experiment | Default no DR; gather chain duration/reaction evidence. Alternate test flag is not a second normal rule |
| Classes | Six draft new classes | Preserve delivered classes/economic tools; six named equal-budget templates are additional presets/teaching kits, all their skills universally book-learnable |
| Spell variants | Future manuscripts proposed | Model family identity and reserve schema; no variants required for v0.7; never grind duplicate books |

## Audit before mutation

Record actual v0.6 source commit, engine/toolchain, build commands, movement/ability framework, body/contender life IDs, private view models, character presets, base formulas, XP table, legacy skill costs/cooldowns, Witness acquisition, death/bank behavior and E01–E48 status. If v0.6 is not actually implemented, complete its required dependencies first and report that scope; the documentation does not assert delivery. Keep same engine and existing ability architecture unless actual integration requires a change. No automatic GAS migration.

Use a clean match boundary and versioned v0.7 config. No mid-match saved-state conversion. Keep v0.6 profile separately selectable for regressions, inaccessible as a source of resources/skills in v0.7. Quick references 1–3 retain compatible IDs when loading local UI preferences; old slot 4 becomes unbound with a clear prompt. Skill slot 5 binds D after local conflict detection, never silently overwrites a user's custom binding. No profile switching in a live match.

Existing legacy classes remain valid starts. Adopted legacy starting attribute totals must be recorded; if they differ from the 53-point new templates, normalize all normal presets to the same budget while preserving primary/secondary direction, then rerun every old score threshold and Witness route. No silent renaming of Laborer/Rogue/Scholar into six new classes. Economic utilities retain learning access and compete for the same five slots. New templates start knowing their two kit skills at Rank 1, equipped in slots 1–2, plus any audited universal basic actions, with no new gold/gear endowment. Preserve existing finite normal consumable endowments once; do not grant them again on respawn or opening a kit menu.

## Explicitly deferred

Public hosting/accounts/matchmaking, production reconnection redesign, squads and diplomacy, full guard/law system, new chat/dialogue, procedural four-city map, full caravans/escorts/insurance, industrial production, random affixes, crit/evasion expansion, item durability, portable assembly, disassembly, subclass trees, spell variants, combat telemetry replay viewer, and full-session balance. Existing retained theft RNG remains legal. These deferrals do not remove four-route integration from v0.7 scope.
