# B67 — v0.6 closeout and transition

24 October 2026. **v0.6 development is closed for transition to v0.7, with explicit
verification deferrals.** The user requested: “proceed with B67, wrap up v0.6,
avoid lengthy tests, keep verifications minimal. defer human review, we are
moving onto v0.7, prepare for transition and handoff”. This overrides the planned
extended B67 test scope. It does not turn unrun acceptance into passes.

The frozen implementation baseline is **`920c4ae` (B66)**. This B67 change updates
the release record, reconciles E01–E48, verifies existing evidence and prepares
[the v0.7 transition](../V07_TRANSITION.md). No runtime source, config, assets,
profile defaults or local input preferences changed. No push was requested.

## Delivered baseline

- [B61](B61_IMPLEMENTATION.md): physical goods, unique identities/lineage,
  final slot/weight capacity, protected physical banking and causal accounting.
- [B62](B62_IMPLEMENTATION.md): paid official supply; finite regional stock/cash,
  integer ask/bid, one-unit atomic exchanges and explicit stale-quote review.
- [B63](B63_IMPLEMENTATION.md): two finite Crystal sources, exact reserved Toolkit/
  Core crafts, fees/coupons and equipped Survey/Analyze through normal cooldowns.
- [B64](B64_IMPLEMENTATION.md): eight funded institutional production/consumption
  opportunities, protected services, one bounded East shortage and actual native
  road/covered-route checks on both saved maps.
- [B65](B65_IMPLEMENTATION.md): bounded distinct Supply/Industrial endorsements,
  Witness and explicit wallet/bank Charter purchase; one-use activation, respawn
  recovery, public 30s warning/60s opening and interruptible ten-second claim.
  A rival without a Charter wins the retained controlled earned route. Normal
  physical-Seal/four-credit bypasses are disabled.
- [B66](B66_IMPLEMENTATION.md): native own-bag service tabs, read-only timestamped
  visited quotes, exact previews, Treasury review, private/public hints, native
  keyboard/fresh remapped hold and editable-field shortcut handling.

Actual normal play remains the v0.6 economy under the historical `Profile=V05`
selector:25 minutes, level5, four shared skills/four quick references,24 items,
locked1–4 contenders. That selector is not an implemented v0.7 profile. The normal
frontend and `Scripts/OpenStandalone.ps1` launch the source Editor game. The old
`Artifacts/20261022-220153-009-PackageWin64/Archive/Windows` predates B61–B66;
it is not a current v0.6 distribution. No new packaged executable is delivered.

## Minimal verification performed now

[Portable audit](evidence/B67/Audit.json), from
`Artifacts/20261024-121555-B67-Closeout`, records:

- All 189 physical hashes in B66’s SourceManifest match. Git reports no changes
  under Source/Config/Content/Scripts or the project file relative to B66.
- All 14 retained report hashes and13 retained original PNG hashes match B66
  provenance. The capture manifest describes39 screenshots; only13 originals
  are retained in Git. Missing unretained local artifacts are not needed to
  interpret the portable report.
- All 13 retained v0.6 ledger files replay successfully using
  `Scripts/CheckEconomyEvidence.py`. These include snapshots and repeated
  milestone evidence, not13 new independent game scenarios.
- v0.6 document-model QA:229 configured quote states plus bank/cargo/timing
  arithmetic. v0.7 specification QA:931 assertions for its proposed catalogs.
  Both are document/data checks, not fresh engine acceptance.
- All48 acceptance rows are reconciled in [B67_ACCEPTANCE](B67_ACCEPTANCE.md)
  and the [working ledger](FeatureLedger.csv):1 source/document audit,
  27 rows with local engineering evidence,17 partial rows and3 wholly deferred
  scenarios. No row classification claims the entire release passed.

No new Unreal process, build, bootstrap, cook/package, network, full session,
restart/performance or human run was needed for this documentation-only closeout.

Retained final B66 evidence, whose runtime inputs still match:

| Lane | Actual result retained | Artifact |
| --- | --- | --- |
| Editor build |4 actions,16.91s, UE5.8.4 CL58902052, cap4 workers | `Artifacts/20261023-233047-421-BuildEditor` |
| Native rendered UI |84 checks each in 1280×720,125% text at 1280×720,1920×1080;39 PNGs; zero failures | `Artifacts/20261023-233109-122-B66-EconomyUI` |
| Rules / authority / earned route |23 /56 /39 checks; zero failures | `Artifacts/20261023-233344-289-B65-Charter` |

The UI fixture uses labelled diagnostic resources/progression/window setup.
The separate earned lane disables diagnostic items and earns its resources
through actual Witness/couriers/paid inputs/crafts/orders/checkout, leaving15g
and300XP. Its placement/clock/legal visit-boundary samples are controlled; it
is not a normal timed or human/remote journey. Independent replay retains the
477 aggregate gold equation450+205 minted−178 burned. Authority boundary fixtures
and UI resource grants remain labelled separately. Older milestone evidence keeps
its original source/fixture limitations; only B66 is the frozen final runtime.

## Deferred verification backlog

These are explicit unrun/partial checks carried into v0.7, not remaining automatic
work for this closeout. Human review is deferred by the user. Extended technical
runs are deferred under the same minimal-verification direction.

| ID | Deferred scope | Transition handling |
| --- | --- | --- |
| V06-D01 | Independent native clients, restricted packet privacy, measured100/150ms RTT and up to25ms one-way jitter; remote economy/input/life races | B70 records inherited gap; affected focused checks during B71–B76, integrated evidence at B77 |
| V06-D02 | Fresh cold checkout/build, current cook/package, packaged launch and independent clients (E38) | Release/distribution remains unverified; B77 must produce matching profile/build evidence |
| V06-D03 | Normal25-minute sessions at one/four contenders, scarcity, hauling margins and distinct approaches (E34; partial E09/E20/E40) | Keep legacy evidence gap separate from new 35-minute v0.7 demonstrations |
| V06-D04 | Ten restarts, normal session, frame/transaction/query/memory measurements and lifetime leaks (E39) | Run only when scheduled; compare actual profile and baseline, no performance pass inferred |
| V06-D05 | Human task review, economy understanding, strategic choices and balance | Explicitly deferred; no invented participants, no repeated request during this transition |
| V06-D06 | Exhaustive live competing market/craft/order/peer/theft/death permutations and endpoint/network races | Retain tested local boundaries; add focused cases when shared authority changes |
| V06-D07 | Physical mouse/keyboard, OS scaling/window placement, full modal/drag/accessibility permutations and rendered routine quality | B66 offscreen/native dispatch is partial evidence; human/OS acceptance stays deferred |
| V06-D08 | Complete normal class-by-class Witness/economic journeys | Audit old class adapters and new 53-point normalization at B70; do not substitute fixtures for normal routes |

E35/E37/E40 retain useful local/earned evidence but are partial. E34/E38/E39 are
wholly deferred for the current v0.6 release. Legacy v0.4/v0.5 packages/network
reports cannot substitute for those economy-version checks.

## Reproduction and transition

Fast document/data checks (no Unreal launch):

```powershell
python docs/v06/tools/check_model.py
python docs/v07/tools/check_spec.py
python Scripts/CheckEconomyEvidence.py docs/v06/evidence/B66/JourneyLedger.json
```

The existing brief engine runners remain available for a material runtime change:
`Scripts/BuildEditor.ps1 -MaxParallelActions 4`, `Scripts/RunCharterCheck.ps1`,
`Scripts/RunEconomyUICheck.ps1` (or focused Small/Large/Wide). They were not rerun
at B67. Keep Unreal operations sequential and Source/Config frozen during them.
Do not automatically launch soak, normal-duration, restart loops or cook/package.

Proceed to **B70 baseline reconciliation**, using [V07_TRANSITION](../V07_TRANSITION.md).
The supplied master revision7/v0.7 files were pre-existing local user changes;
they remain untouched and unstaged by B67. Their exact supplied bytes are recorded
in [SuppliedSpecSnapshot](evidence/B67/SuppliedSpecSnapshot.json). This is design
provenance, not v0.7 runtime completion. No engine migration, blanket GAS rewrite,
profile conversion or speculative new feature is part of this closeout.
