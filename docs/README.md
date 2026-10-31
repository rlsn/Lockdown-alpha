# Project Lockdown documentation

The active implementation is **v0.9 B92 population/economy**, following
completed B90 spatial and B91 functional-environment work from native v0.8 B86. Start with the
[continuation](CONTINUATION.md) for current status, next work and commands, or
with the [living master](master/README.md) for the long-term game design.

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

## B91 checkpoints

| Report | Verified boundary |
| --- | --- |
| [Interiors and doors](v09/B91_IMPLEMENTATION.md) | Structural walks, permanent stairs, authoritative doors and restricted observation |
| [Storage and cargo](v09/B91_OBJECTS.md) | Finite ownership, private storage and legal world-cargo placement |
| [Portable tools](v09/B91_TOOLS.md) | Retained physical fuel and cosmetic light |
| [Approved traversal](v09/B91_TRAVERSAL.md) | Shared ladder/Rope links and supported interrupted movement |
| [Fixed furniture](v09/B91_FURNITURE.md) | Empty private storage with separate walking and sight behavior |
| [Saplings](v09/B91_SAPLINGS.md) | Original finite harvest/chop/melee milestone; later ignition/completion and source controls supersede its original hold-key binding |
| [Paid fields](v09/B91_FIELDS.md) | Water/Oil release, support planes, allocator limits and restricted field views |
| [Socket ignition](v09/B91_IGNITION.md) | Finite Torch fuel, reserved fire cells, sapling clearance and Oil ignition |

The [B91 completion review](v09/B91_COMPLETION.md) closes the functional-environment boundary. The working ledger keeps later population, economy and match acceptance separate.

## B92 progression checkpoints

| Report | Verified boundary |
| --- | --- |
| [Roster](v09/B92_ROSTER.md) / [services](v09/B92_SERVICES.md) | Persistent seats and role bodies, remote ownership, physical finite commodity transactions |
| [Bank](v09/B92_BANK.md) / [supply](v09/B92_SUPPLY.md) | Persistent protected money, explicit fees, paid catalog goods/books and shared rare quotas |
| [Deposits](v09/B92_DEPOSITS.md) / [Survey](v09/B92_SURVEY.md) | Automatic eligible extraction and a real prepared skill using the same finite budget |
| [Work](v09/B92_WORK.md) / [courier](v09/B92_COURIER.md) | Exact crafting/procurement and persistent physical parcel lifecycle |
| [Earned Witness/Charter](v09/B92_CHARTER.md) | One earned book/preparation/work/proof/registration chain; macro travel remains staged |
| [Civilian/traveler routines](v09/B92_ROUTINES.md) | Native path-following, staggered activity, observed flee/resume and visual-only sealed logistics |

B92 remains in progress. Security, hostile behavior, legal autonomous contenders, natural
pacing and broad stress/recovery remain open. B93 activation/Heart and B94 full
session/human acceptance are separate; `v09_full` stays closed.

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
