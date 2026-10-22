# Project Lockdown v0.5 UI and control sprint

Specification revision 1 • 21 October 2026 • Extends the delivered v0.4 Unreal game

Make the existing game consistent to operate: right-click navigation, left-click inspection, an anchored own command bar, a fog-aware minimap, a larger inventory, quick-item references and actual vendor and peer trade. Preserve the class, skill, growth, sight and Grand Heart systems.

The actual v0.4 Unreal source, assets and B47 evidence are present in this repository. The [baseline review](B50_BASELINE_REVIEW.md) records the inspected revision and adopted defaults. [B51 navigation](B51_IMPLEMENTATION.md), [B52 input/action modes](B52_IMPLEMENTATION.md), [B53 HUD/inspection/minimap](B53_IMPLEMENTATION.md), [B54 inventory/quick references](B54_IMPLEMENTATION.md) and [B55 quoted Guild vendor](B55_IMPLEMENTATION.md) implement the current checkpoint. B56–B58 remain pending. This specification and its wireframes describe the complete target. Follow the [current continuation](../CONTINUATION.md) and [working feature ledger](FeatureLedger.csv).

## Reading order

| File | Purpose |
| --- | --- |
| [B50_BASELINE_REVIEW.md](B50_BASELINE_REVIEW.md) | Actual baseline, review findings, adopted defaults and first implementation gate |
| [B51_IMPLEMENTATION.md](B51_IMPLEMENTATION.md) | Native navigation adapter, cooked proof, corrections and review boundary |
| [B52_IMPLEMENTATION.md](B52_IMPLEMENTATION.md) | Mapping, targeting, panel routing and actual input evidence |
| [B53_IMPLEMENTATION.md](B53_IMPLEMENTATION.md) | Masked inspector, minimap, life/results and approved 5.8.4 baseline |
| [B54_IMPLEMENTATION.md](B54_IMPLEMENTATION.md) | Carried inventory, finite supplies, identity references and native/cooked proof |
| [B55_IMPLEMENTATION.md](B55_IMPLEMENTATION.md) | Quoted Guild vendor, earned purchase/read and conservation evidence |
| [FeatureLedger.csv](FeatureLedger.csv) | Working baseline and implementation status; templates remain separate |
| [01_SCOPE_AND_MIGRATION.md](01_SCOPE_AND_MIGRATION.md) | Sprint boundary and explicit replacements |
| [02_INPUT_NAVIGATION_AND_CAMERA.md](02_INPUT_NAVIGATION_AND_CAMERA.md) | Complete controls, orders, focus and cancellation |
| [03_HUD_SELECTION_AND_MINIMAP.md](03_HUD_SELECTION_AND_MINIMAP.md) | Screen hierarchy and authorized inspection |
| [04_INVENTORY_AND_QUICK_ITEMS.md](04_INVENTORY_AND_QUICK_ITEMS.md) | Real item ownership and a larger carried bag |
| [05_VENDOR_AND_PEER_TRADE.md](05_VENDOR_AND_PEER_TRADE.md) | Catalog purchase and consensual atomic exchange |
| [06_PRESENTATION_AND_ACCESSIBILITY.md](06_PRESENTATION_AND_ACCESSIBILITY.md) | Shared widgets, copy, layout and wireframes |
| [07_UNREAL_CONTRACTS_AND_AUTHORITY.md](07_UNREAL_CONTRACTS_AND_AUTHORITY.md) | Native integration, view models and transaction rules |
| [08_BUILD_PLAN.md](08_BUILD_PLAN.md) | B50–B58 implementation sequence |
| [09_ACCEPTANCE_AND_USER_REVIEW.md](09_ACCEPTANCE_AND_USER_REVIEW.md) | U01–U56 engineering and usability checks |
| [10_CODEX_BUILD_PROMPT.md](10_CODEX_BUILD_PROMPT.md) | Development handoff prompt |
| [11_SOURCE_AND_DECISIONS.md](11_SOURCE_AND_DECISIONS.md) | User direction, source limits and proposed defaults |
| [data/ui_control_contract.json](data/ui_control_contract.json) | Mapping and behavior companion, not an executable rules engine |
| [wireframes/README.md](wireframes/README.md) | Annotated HUD, inventory and trade diagrams |
| [templates/FEATURE_LEDGER.csv](templates/FEATURE_LEDGER.csv) | Baseline, changes and evidence template |
| [templates/IMPLEMENTATION_REPORT.md](templates/IMPLEMENTATION_REPORT.md) | Delivery and review report |
| [reference-v04/README.md](reference-v04/README.md) | Frozen predecessor contracts |

## Release demonstration

Host and join without a console. Navigate around a ramp and trees by right-click. Select a stranger and understand what is known without controlling or exposing them. Prepare four skills, learn and read a real book, put a finite ward on a quick slot, and use it through the same item path as inventory. Purchase a book and complete or cancel a real peer exchange. Lose sight, lose focus and die without stale selection, movement, duplicated valuables or accidental casts. Earn the Heart under the retained normal progression rules.

The normal profile remains 1–4 humans, level five, four shared skill slots and a 25-minute integration cap. New proposed reductions are 24 inventory slots, four quick-item references and a bounded peer trade of up to six items per participant plus gold. The full game still targets 60–90 minutes. No mana, extra spells, equipment stats, stack system, full market or full conquest port is added.

## Handoff authority

Use master plan revision 5 for long-term intent and this package for the sprint. v0.4 remains the gameplay baseline except explicit navigation, input, inventory-capacity and voluntary trade changes. Prose controls behavior; keep JSON and actual assets synchronized. A wireframe is a design reference, not proof of implementation. Read the local baseline review before B51; the proposed charge-drop fallback does not replace the audited loss-on-death rule.
