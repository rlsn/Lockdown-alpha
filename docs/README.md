# Project Lockdown documentation

The active implementation is **v0.9 B91 functional environment**, starting from native v0.8 B86 at ebc19a5 after completed B90 spatial work. [B91 progress](v09/B91_IMPLEMENTATION.md), [portable tools](v09/B91_TOOLS.md) and [approved traversal](v09/B91_TRAVERSAL.md) and [fixed furniture](v09/B91_FURNITURE.md) distinguish verified checkpoints from remaining acceptance. [Continuation](CONTINUATION.md) gives the next steps and commands.

## Design and active implementation

| Reference | Purpose |
| --- | --- |
| [Living master design](master/README.md) | Long-term vision, principles, core loop, mechanics, control/UI, world and player experience |
| [v0.9 specification](v09/README.md) / [stage plan](v09/09_IMPLEMENTATION_STAGES.md) | Preserved full-map contracts; canonical B90–B94 sequence |
| [B90 baseline review](v09/B90_BASELINE_REVIEW.md) | Actual architecture, starter/Witness audit and isolation decisions |
| [B90 traversal](v09/B90_TRAVERSAL.md) | Actual journeys, physical border/terrace tests and measured density risks |
| [B90 implementation](v09/B90_IMPLEMENTATION.md) | Native changes, commands, measured evidence and remaining gates |
| [v0.9 working ledger](v09/FeatureLedger.csv) | Per-case actual status; separate from blank source templates |
| [Source adoption](v09/SOURCE_MANIFEST.json) | Hashes of the supplied untracked specification package |

## Retained release records

| Release | Evidence and scope |
| --- | --- |
| v0.8 | [B86 report](v08/B86_IMPLEMENTATION.md), [playtest limits](v08/B86_PLAYTEST.md), [ledger](v08/FeatureLedger.csv), [B85 earned routes](v08/B85_IMPLEMENTATION.md), [B84 catalog/acquisition](v08/B84_IMPLEMENTATION.md), [spec](v08/README.md) |
| v0.7 | [B77 closeout](v07/B77_CLOSEOUT.md), [acceptance](v07/B77_ACCEPTANCE.md), [ledger](v07/FeatureLedger.csv), [spec](v07/README.md) |
| v0.6 | [B67 closeout](v06/B67_CLOSEOUT.md), [acceptance](v06/B67_ACCEPTANCE.md), [ledger](v06/FeatureLedger.csv), [spec](v06/README.md) |
| v0.5 | [B58 handoff](v05/B58_HANDOFF.md), [spec](v05/README.md) |
| v0.4 | [Implementation](v04/IMPLEMENTATION_REPORT.md), [spec](v04/README.md) |
| v0.3 | [Migration implementation](v03/implementation/README.md), [spec](v03/README.md) |

Original specifications/reference archives and historical evidence remain frozen. Runtime reads staged Content/native assets, never documentation catalogs. Local builds/packages/captures live in ignored repository-root Artifacts; portable selected evidence lives with its release.

Spec/model validation, native paths, actual movement, restricted clients, ledgers, performance and human observations establish different things. Preserve their stated profile/build/setup/limits. Missing human, remote distribution, full-duration and inherited acceptance remains open even when a focused engineering check passes.
