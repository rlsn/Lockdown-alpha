# Project Lockdown v0.4 development request

Implement the class-and-progression step defined in this package on top of the actual Unreal v0.3 project. Read the revision 4 master plan, all v0.4 specifications, source decisions, JSON tuning and ledger template before editing. This request authorizes implementation and verification, not only a plan.

The new long-term direction removes predefined heroes. Starting class supplies attributes and competencies; players allocate six attributes, learn and rank skills, specialize and prepare one shared pool of combat and utility actives. Books support cross-class knowledge. Full matches now target 60–90 minutes and maps are flexible regional worlds. v0.4 deliberately tests a smaller profile and must not claim full-duration validation.

## Audit first

Inspect the actual Unreal revision, engine pin, C++ and asset state, packaging, movement, authority and private views. Do not assume v0.3 completion from its documents. Repair required gaps explicitly while preserving working systems. Do not start a new engine, transport or mandatory GAS migration. If GAS already exists, extend it coherently; otherwise retain a focused implementation.

## Implement the proposed release

Build Laborer, Rogue and Scholar class starts with equal 60-point budgets and STR DEX CON INT WIS CHA. Implement five levels, allocated points, class skill learning, ranks and a private level-four subclass choice. Use exact proposed formulas and costs initially. No primary-attribute growth or old random-upgrade bonuses remain active.

Create a library separate from four equipped slots. Every learned active, combat or utility, uses those same slots. Implement the nine catalog skills and persistent cooldown identity. Replace old global dash and assassination shortcuts with equipped Step and Ambush. Context menus and direct RPCs cannot bypass equipment. Basic actions stay explicitly separate.

Add real book purchase, inventory, interrupted reading, cross-class rank-one learning and preparation commits. A Rogue must be able to learn Spark. A library with more than four skills must require a choice, and swapping must not clear cooldowns or heal a character.

Implement four deterministic Charter alternatives, one server-resolved Pickpocket roll with stable retry windows and finite wallet transfer, actual consequences and no private target-stat preview. Shared humanoid wallet validation must not become a human-versus-NPC oracle. Roll only once on a valid completion and never on preview or replay.

Author one city, two towns and wilderness using region data, with public and covered paths, a small peaceful crowd, a shared-rule trained NPC, paid courier work, first visits, one bounded sentinel and real Analyze sabotage of a public source. Keep geometric fog and private attributes separate from perception checks.

Close the real nonviolent route: one equipped-skill Charter Witness, four work credits, 100 gold, actual fabrication and a real ten-second Heart claim. Remove ordinary seeded Seals. Preserve unique ownership, death drops and recovery by another contender. Keep full conquest explicitly deferred and the central gate protected in the normal profile.

Use the 25-minute integration cap, character level five and four shared slots as declared prototype reductions. The sixty-to-ninety-minute direction belongs in the master plan and future pacing work. Log all overrides in a separate technical soak. Do not expand to the complete four-city world, factions, squads, crits or broad content.

## Authority and delivery

Keep character development on the persistent contender, not the temporary pawn. Validate owned life, build revision, skill equipment, costs and geometry before outcomes. Commit points, books, gold, rolls, Witness, fabrication and results atomically. Retain knowledge, allocations and cooldowns through death, while physical unread books and a Seal drop.

Run B40–B47 and C01–C48. Keep legacy foundation regressions where unchanged and explicitly replace changed fixed-kit and progression expectations. Test actual remote-client payloads and actor state for private-build leakage, not only screenshots. Generate real Unreal assets and verify cold build, cook, package, multiplayer and restart behavior.

Deliver runnable source and assets, synchronized versioned tuning, scripts, completed baseline and feature ledger, actual test logs, a normal-rule demo guide, known limits and the next parity tasks. Label tests Passed, Failed, Not run or Conditional not available. If missing tools block compilation, finish feasible work and report the precise blocker without fabricating a build. Human review may remain pending but cannot be replaced by invented participants or a bot result.

The final result should let testers create different builds, learn across classes, make a mixed-slot choice, see scaling and check consequences, and earn the Heart under real progression rules. Keep synced project sources read-only.
