# Project Lockdown v0.6 economy prototype
Specification revision 1 • 23 October 2026

Extend the user-reported delivered v0.5 Unreal game with a hybrid economy: stable official shops, finite regional markets, light production, visible institutional demand and a production-earned route to the contested Grand Heart.

This is a proposed development contract, not a game build. Audit the actual delivered repository; no Unreal source or results were supplied in this workspace.

| Document | Purpose |
| --- | --- |
| [01_SCOPE_AND_BASELINE.md](01_SCOPE_AND_BASELINE.md) | Retained systems and explicit replacements |
| [02_SHOPS_AND_REGIONAL_MARKETS.md](02_SHOPS_AND_REGIONAL_MARKETS.md) | Goods, cash, stock, quotes and prices |
| [03_PRODUCTION_SKILLS_AND_NPCS.md](03_PRODUCTION_SKILLS_AND_NPCS.md) | Gathering, recipes, utility and institutional consumption |
| [04_MACRO_ACCOUNTING.md](04_MACRO_ACCOUNTING.md) | Gold/item sources, sinks, budgets and invariants |
| [05_PROGRESSION_AND_HEART_ROUTE.md](05_PROGRESSION_AND_HEART_ROUTE.md) | Distinct endorsements, Charter and public contest |
| [06_UI_AUTHORITY_AND_TRANSACTIONS.md](06_UI_AUTHORITY_AND_TRANSACTIONS.md) | Native presentation, privacy and atomic commits |
| [07_BUILD_PLAN.md](07_BUILD_PLAN.md) | B60–B67 development sequence |
| [08_ACCEPTANCE_AND_PLAYTEST.md](08_ACCEPTANCE_AND_PLAYTEST.md) | E01–E48 checks and human evidence |
| [09_CODEX_BUILD_PROMPT.md](09_CODEX_BUILD_PROMPT.md) | Ready-to-use development request |
| [11_BANKING_AND_CARGO.md](11_BANKING_AND_CARGO.md) | Physical bank, protected wealth and weight limits |
| [10_SOURCES_AND_DECISIONS.md](10_SOURCES_AND_DECISIONS.md) | Source limits and proposed decisions |
| [data/economy_rules.json](data/economy_rules.json) | Machine-readable companion |
| [tools/check_model.py](tools/check_model.py) | Reproducible specification arithmetic checks |
| [MODEL_VALIDATION.md](MODEL_VALIDATION.md) | Completed document/model QA, separate from game tests |
| [templates/FEATURE_LEDGER.csv](templates/FEATURE_LEDGER.csv) | Baseline/change/evidence template |
| [templates/IMPLEMENTATION_REPORT.md](templates/IMPLEMENTATION_REPORT.md) | Report and reconciliation template |
| [reference-v05/README.md](reference-v05/README.md) | Frozen predecessor contracts |

The complete assistant discussion is now included as source; see [source provenance](reference-discussion/README.md).

The normal profile remains 1–4 roster-locked humans, 25 minutes, level five, 24 carried instances, four shared skill slots and four item references if those defaults were delivered. Actual adopted v0.5 deviations must be recorded rather than overwritten silently. Full-game sessions still target 60–90 minutes.

Success is real seed capital → supply/transport → two-step production → distinct commercial endorsements → registered Charter → public contested claim, with conservation evidence and no normal diagnostic grants. A player must also be able to choose regional trading or gathering instead of only repeating one recipe. Simple banking, cargo weight and a compressed public Charter opening are included. No dialogue, full auction, credit, equipment tree, engine migration or autonomous rival economy is required.

## Repository implementation record

[B60](B60_BASELINE_REVIEW.md) audited the actual B57 source and retained evidence.
[B61 is complete and verified](B61_IMPLEMENTATION.md): cargo, runtime catalog,
institution/node/budget state, physical banking, causal accounting, physical
reconciliation and reset evidence. All 22 native rules and 30 standalone
engineering checks passed; exported receipts also passed independent replay.
[B62 commerce](B62_IMPLEMENTATION.md) now provides paid material supply and
finite stock/cash Buy/Sell; 22 rules and 26 two-owner authority checks passed.
[B63 production](B63_IMPLEMENTATION.md) now provides finite gather, exact-ID
reserved crafting, native previews and equipped Survey/Analyze coupons; 23 rules,
49 authority checks and independent replay passed. B64 schedules/geography is next.
Native input/layout/network, normal progression and release acceptance remain open. Earlier partial reports are historical checkpoints, not current
completion status. Use the [working ledger](FeatureLedger.csv).
