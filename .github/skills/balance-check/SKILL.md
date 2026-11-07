---
name: balance-check
description: Analyze Lockdown combat, progression, economy, loot choices, and objective pacing for numerical outliers and degenerate strategies. Use for game balance checks or evidence-backed tuning proposals, not to approve or automatically apply new numbers.
license: MIT
metadata:
  source: https://github.com/Donchitos/Claude-Code-Game-Studios/blob/05ad7c8a5b8aa2aa86bbd3439ae7cf7295cac418/.claude/skills/balance-check/SKILL.md
---

# Lockdown balance check

Adapted from Claude Code Game Studios; the license is in
`.github\LICENSE.agent-customizations`.
This is an analysis workflow, not an automatic tuning or balance-certification
tool. It does not depend on Claude commands, hooks, models, or templates.

## 1. Bound the question and establish sources

Accept a system name, a repository file, or a specific balance question.
If the scope is genuinely unclear, ask one focused question using the
available question tool. Do not review unrelated domains just for completeness.

Read `README.md` and the relevant sections of `docs\APP_SPEC.md`, then:

- `docs\demo\CONTENT_AND_BALANCE.md`: approved numbers, units, formulas,
  decision status, model assumptions, and tuning proposals.
- `docs\DEVELOPMENT.md` and the affected `docs\systems` manual: real
  interfaces, generation commands, checks, and evidence boundaries.
- `SourceData\Demo\content.json` and its actual schema: current authoring
  inputs. For spatial or camp questions, also inspect
  `SourceData\World\demo_world.json`.
- Relevant runtime implementation and generated payloads when needed to
  verify interpretation. Do not replace approved inputs with test fixtures.
- Existing playtest evidence only when it is supplied or located in the
  current workspace. Trace it to its build and content revision.

For cross-system questions, also consult the readiness report,
implementation plan, and shared contracts under `docs\demo`.
Read current files, not historical versions, to establish the baseline.

List each required input as FOUND or ABSENT. Record the exact source location,
stable content IDs, and decision status for numbers used in the analysis.
Later approved decisions can supersede earlier examples in the same document;
resolve this explicitly. Unapproved proposals are not the running baseline.

If no relevant input exists, stop with `NOT ASSESSED - NO DATA` and identify
what is missing. Missing data is not a clean pass.
An explicitly requested input file is required for that invocation. If it is
absent, stop with that verdict; do not substitute another catalog or sample
just because other repository data exists.

## 2. Establish targets and calculation semantics

Find stated, approved targets for the requested question. A schema bound or
overflow limit is not automatically a desirable balance range.
If targets are absent, mark target-dependent comparisons
`NOT ASSESSED - NO TARGET`; still report independently computable facts.
Never invent a target DPS, TTK, session duration, drop rate, or gold budget.

Verify units, caps, stacking, rounding, and timing against the actual
implementation before calculating. Include windup and recovery, cooldown
start rules, resource costs, damage mitigation, conditional triggers,
source-specific cooldowns, and discrete server-time behavior where relevant.
Do not use tooltip text or `Unavailable` alone as a readiness oracle.

Use exact integers, rational arithmetic, or decimal arithmetic as appropriate
to the repository's semantics. State any approximation and its error boundary.
For each conclusion, provide the inputs, formula or executable calculation,
result, and assumptions so another contributor can reproduce it.

## 3. Analyze the requested domain

Only run the checks the question and available evidence support.

### Combat and builds

- Compare damage, healing, shields, mitigation, control, mobility, range,
  resource use, and objective pressure over a stated encounter scenario.
- Compute DPS and TTK at relevant levels and builds using real attack
  intervals and mitigation; show burst separately from sustained damage.
- Check strict dominance, infinite sustain, control loops, and relevant
  counters. State the conditions rather than asserting universal dominance
  from one favorable matchup.
- Inspect stacking, slot replacement, selling and reacquiring, death, and
  source loss. Preserve existing authority, lifecycle, and receipt semantics.

### Economy

- Map gold and XP faucets and sinks across the actual match phases.
- Calculate resource flow, purchase timing, alternatives, and opportunity
  costs under explicit farming and combat assumptions.
- Check buy/sell and repeat-reward loops against the transaction rules.
  Do not assume a finite total reward budget in a match with repeating camps.
- Distinguish an algebraic exploit from an incentive that needs playtesting.

### Progression and choices

- Compute the real XP thresholds, time-to-level assumptions, power changes,
  and decision opportunities for relevant builds.
- Examine dead zones, spikes, offered choices, rerolls, replacements,
  acquisition probabilities, and slot pressure.
- Use the real candidate-selection algorithm. Do not add pity, rarity,
  slot limits, or independent random draws that the approved design lacks.

### Camps, space, and the victory objective

- Analyze clear cost and rewards, return trips, respawn timing, and pressure
  between development, shopping, spraying, guarding, and contesting.
- Use legal paths and approved query semantics, not straight-line distance
  as a substitute for navigation. Include unavailable spatial evidence.
- Do not invent an alternate victory condition, team economy, or catch-up
  mechanism. Mark the actual human-client and AI scenarios still needed.

## 4. Report results and uncertainty

Use a concise report with sources, calculations, findings, recommendations,
and missing evidence. For findings, use a table:

| Finding | Scenario and source | Computed result | Approved target | Evidence or limitation |
| --- | --- | --- | --- | --- |

Choose an overall result:

- `CRITICAL ISSUES`: evidence demonstrates a broken progression, repeatable
  resource exploit, or a strictly dominant strategy within a stated scope.
- `CONCERNS`: measured deviations or plausible interaction risks require
  further tuning or validation.
- `NOT ASSESSED`: inputs or required targets are missing; name the gaps.
- `NO ISSUES FOUND IN ANALYZED MODEL`: stated comparisons passed within the
  documented model. This is not a declaration of gameplay balance.

Measured problems take precedence over missing-evidence classifications;
report missing evidence even when the overall result is a finding.
Separate arithmetic results, executed automated checks, actual playtests,
unrun scenarios, and unimplemented behavior. Do not reuse an old report as
evidence that a current build passed.

## 5. Propose, do not apply

Recommend changes only with evidence and a clear account of their costs and
cross-system consequences. Missing targets require a user decision, not a
temporary default. Give the smallest real playtest or targeted check that
would distinguish the alternatives.

This invocation is read-only. Do not edit approved numbers, SourceData,
generated payloads, hashes, assets, or code. Do not automatically run Sync,
Bootstrap, imports, or MCP mutations.
If the user approves a subsequent tuning task, record their exact decision
on its issue and follow the existing content-design and generation pipeline.

Keep task reports on the issue or PR, or as issue artifacts when available;
local calculation and runtime outputs belong in ignored `Artifacts`.
Do not create `design\balance`, a shadow registry, or a session log.
