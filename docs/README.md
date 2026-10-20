# Project Lockdown documentation

Active release: **v0.4 development**. B40 baseline verification has passed, and
B41 adds private character state and a Ready lobby; B42 adds equipped skill
execution and a mixed hotbar; B43 adds preparation, subclasses and physical books
on the retained migration map.
The complete class/skill/region/acquisition loop remains in development.

| Start here | Purpose |
| --- | --- |
| [Continuation](CONTINUATION.md) | Current handoff and next implementation steps |
| [Master plan, revision 4](master/README.md) | Long-term design authority and current user decisions |
| [v0.4 specification](v04/README.md) | Release scope, proposed tuning, B40–B47 and C01–C48 |
| [v0.4 baseline review](v04/BASELINE_REVIEW.md) | Source inspection, existing evidence and migration risks |
| [B40 audit](v04/B40_BASELINE_AUDIT.md) | Fresh build, native and packaged baseline results |
| [B42 execution](v04/B42_IMPLEMENTATION.md) | Equipped skills, held channels and checkpoint evidence |
| [B43 preparation](v04/B43_IMPLEMENTATION.md) | Private build preview, held commits, books and checkpoint evidence |
| [v0.4 implementation record](v04/IMPLEMENTATION_REPORT.md) | Current implementation and verification boundary |
| [v0.4 feature ledger](v04/FeatureLedger.csv) | Working inventory; baseline observations and open implementation work |
| [v0.3 implementation archive](v03/implementation/README.md) | B32–B36 reports, tests, demo, audits and migration ledger |
| [v0.3 specification](v03/README.md) | Historical migration requirements and frozen v0.2 references |

Read the master plan for intent and the active release spec for implementation
scope. Historical specifications, fixture demonstrations and old test results do
not override v0.4 rules or establish its acceptance.

Implementation reports belong under their release directory. Keep templates
blank and separate from working ledgers and actual results. Local build, package,
screen capture and test evidence remains under ignored repository-root
`Artifacts/`; paths quoted in archived reports still refer to that root.
