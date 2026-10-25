# Project Lockdown documentation

Current checkpoint: **v0.7 B76 native world; B77 acceptance next**.
[B76 implementation](v07/B76_IMPLEMENTATION.md) records the actual runtime,
four normal routes, native verification and remaining scope. The default frontend
still uses V05; an explicit v07 profile opens the native world/combat build. Full
v0.7 and inherited technical/human acceptance remain open.

| Current reference | Purpose |
| --- | --- |
| [Continuation](CONTINUATION.md) | Runnable commands, next work and constraints |
| [B76 implementation](v07/B76_IMPLEMENTATION.md) | Native world, finite encounters, four routes, leases and common result ordering |
| [B75 implementation](v07/B75_IMPLEMENTATION.md) | Historical28 spells/40 items, starters, capstones and effects |
| [B74 implementation](v07/B74_IMPLEMENTATION.md) | Historical acquisition/assembly checkpoint |
| [B73 implementation](v07/B73_IMPLEMENTATION.md) | Historical build/gear/preparation checkpoint |
| [B72 implementation](v07/B72_IMPLEMENTATION.md) | Historical geometry/control checkpoint |
| [B71 implementation](v07/B71_IMPLEMENTATION.md) | Historical combat/MP checkpoint |
| [B70 review](v07/B70_BASELINE_REVIEW.md) | Inspected source, adapters, evidence and dependencies |
| [B70 adapter contract](v07/B70_ADAPTERS.json) | Adopted profile/wire/import/class decisions; historical adoption |
| [v0.7 specification](v07/README.md) | Normative complete release scope, B70–B77 |
| [v0.7 working ledger](v07/FeatureLedger.csv) | Actual C001–C055 status |
| [Inherited acceptance](v07/evidence/B70/InheritedAcceptance.csv) | All48 v0.6 rows and mapped follow-ups |
| [Master plan revision7](master/README.md) | Long-term intent; active release spec governs implementation |

| Retained release record | Purpose |
| --- | --- |
| [v0.6 specification](v06/README.md) | B60–B67 economy/evidence index |
| [B67 closeout](v06/B67_CLOSEOUT.md) / [acceptance](v06/B67_ACCEPTANCE.md) | Frozen runtime, E01–E48 and V06-D01–D08 deferrals |
| [B66 implementation](v06/B66_IMPLEMENTATION.md) | Final native services/UI/economy integration |
| [v0.6 ledger](v06/FeatureLedger.csv) | Historical delivered/partial/deferred status |
| [v0.5 specification](v05/README.md) / [B58 handoff](v05/B58_HANDOFF.md) | Navigation, input, HUD, inventory, vendor and peer trade |
| [v0.4 specification](v04/README.md) / [implementation](v04/IMPLEMENTATION_REPORT.md) | Classes, skills, progression, Witness and region |
| [v0.4 continuation archive](v04/CONTINUATION_B47.md) | Preserved earlier handoff |
| [v0.3 specification](v03/README.md) / [implementation archive](v03/implementation/README.md) | Unreal migration and frozen v0.2 references |

Inherited engine/fixture results retain their stated source/profile and limits.
Missing remote/package/session/human evidence remains open. Human review and
longer technical runs remain named deferred work.

Keep original specifications/reference archives intact. Put implementation
reports and portable evidence under their release directory; keep templates
blank and working ledgers separate. Local build/package/capture artifacts live
under ignored repository-root `Artifacts/`. Runtime reads staged Content and
native assets, never documentation catalogs.
