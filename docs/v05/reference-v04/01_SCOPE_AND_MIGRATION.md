# Scope and migration

## Baseline audit

Record the actual Unreal revision, pinned engine, module and asset state, cold build, packaged run, required two-client behavior and remaining v0.3 ledger gaps. This workspace does not establish v0.3 completion. Missing movement, privacy, life or packaging foundations are prerequisite work; identify their cost separately without rebuilding working systems.

Preserve the engine version and current architecture where compatible. The release changes character design, not the platform. Do not begin another renderer, transport or ability framework rewrite as a side effect.

## Required changes

| Area | v0.4 behavior |
| --- | --- |
| Character start | Choose Laborer, Rogue or Scholar; no hero identity |
| Attributes | STR DEX CON INT WIS CHA, equal initial total and allocated growth |
| Development | Character level 1–5, skill points, class learning, rank improvement and level-four subclass choice |
| Learned versus equipped | Library and ranks persist; four slots permit activation |
| Slot budget | Every learned active uses the same pool, including contextual utility |
| Old global keys | Dash becomes Step; assassination becomes Ambush; no unslotted Space or Q bypass |
| Universal learning | Books teach for the match; cross-class learning works |
| Checks | Bounded combat scaling, deterministic environmental checks and one probabilistic theft |
| World | One city, two towns and wilderness connected through configurable region data |
| Progression loop | Paid commissions, first visits and a bounded wild encounter |
| Objective | Real witness and Seal fabrication; normal acquisition no longer seeded by a developer |
| Session profile | 25 minute integration cap; full design target remains 60–90 minutes |

## Retain

Keep native grounded movement, collision, camera, owner authority, restricted views, wall and tree sight, elevation, source union, life generations, single-result transactions, real assets, scripts and packaged-client workflow. Improve native menus for class selection, development and mixed skills.

Disable the legacy random upgrade pool in this profile, including unslotted Vitality, Fleet, Recovery and Patience bonuses. Do not apply both old growth and new derived stats. Preserve old data only as history or an explicit legacy test profile. Rename hero-level concepts to character level without breaking required serialized assets silently.

Ordinary movement, sprint, basic attack, talk, loot, courier interaction, book reading and objective assembly remain basic actions. A learned active such as Persuasion or Analysis always requires equipment. The finite ward item remains an item, not an unlimited learned ability; a future reusable ward skill would use a slot.

## Release limits

No fixed hero roster, full class tree, full economy, factions, guards, recruits, procedural world, multiplayer squads, PvP kill XP, crit or evasion system, generic crafting or noncentral winning route is required. Full conquest progression remains an inventoried port gap. Keep ordinary Heart access protected in the normal v0.4 profile; a real Seal permits acquisition. Tests may configure conquest flags only through labelled authority fixtures.

The normal profile must not seed Seals, gold, XP, witness receipts or learned books. Test harness grants are allowed for isolated cases, never as evidence that the normal loop is complete.

## Lobby and session scope

Add a class-selection and Ready lobby. Host can start with 1–4 connected humans; one is practice. Starting choices are private. Lock the roster at start to prevent fresh guest entries resetting finite contract and reward budgets mid-match.

If v0.3 has no verified resume mechanism, disconnect withdraws the contender, drops valuables and cancels pending actions; a new guest waits for the next lobby. If authenticated resume already works, retain it with the same contender and budgets, then test it. Do not weaken a working implementation or claim that raw address rejoin restores a private identity. Robust resume remains a prerequisite before full 60–90 minute sessions.

Restart clears attributes, levels, inventory, knowledge, loadouts, receipts, region discovery, NPC purses, cooldowns and ownership. Last-living status never awards a win.
