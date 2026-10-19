# Continuation — v0.4 preparation, 19 October 2026

The revision 4 master plan and v0.4 specification are the active design inputs.
The code baseline is v0.3 checkpoint `4cc9cbde3e696eaef2f0defff1f0ddec48e5ba89`.
This handoff organizes documentation and records a source review; no v0.4 runtime
feature or acceptance check is claimed complete.

## Read first

1. [Master plan](master/README.md), then the complete [v0.4 package](v04/README.md).
2. [Baseline review](v04/BASELINE_REVIEW.md) and [working feature ledger](v04/FeatureLedger.csv).
3. [B36 evidence](v03/implementation/TestReport.md),
   [migration gaps](v03/implementation/MigrationLedger.csv) and
   [known limitations](v03/implementation/KnownLimitations.md).

The former top-level implementation reports now live together in
[v03/implementation](v03/implementation/README.md). Its
[old continuation](v03/implementation/CONTINUATION.md) is historical: the owner's
move to v0.4 supersedes its instruction to stop gameplay expansion for B37 human
review. Carry that pending review into the new acceptance record.

## Next implementation work

Finish **B40** before B41: record fresh cold Editor/Game builds, asset validation,
native rules and relevant movement, private-view, combat, life and packaged-client
regressions. Preflight passed during this preparation; these runtime checks were
not rerun. Existing B36 logs remain prior evidence. Preserve the pinned Unreal
5.8.3 CL 58210709 engine and the focused C++ architecture.

Then follow B41–B47 in [build order](v04/07_BUILD_PLAN.md): private class/level state
and lobby; one equipped-skill activation path; preparation, books and subclasses;
checks and theft; connected region and bounded rewards; earned Seal; delivery.
Use the working ledger rather than editing the blank template.

Address the inspected migration risks explicitly:

- Replace integer HP/damage consistently for fractional scaling across combat,
  owner state, UI and tests.
- Parameterize fog bounds, cell addressing, serialization limits and minimap
  mapping for the larger region; both towns lie outside the current grid.
- Gate every Step/Ambush/context entry point through the four-slot loadout.
- Replace old XP/death/deadline rules, fixed work destination and diagnostic
  acquisition with the declared v0.4 profile and normal earned route.
- Add a Ready lobby and roster lock; current guest login creates a fresh record
  and logout removes it. Authenticated resume is absent.

## Evidence and workflow

The normal v0.4 profile is 1–4 humans, five levels, four slots, nine skills, one
city/two towns and a 25-minute cap. The 60–90 minute goal is future pacing work;
the 90-minute harness soak is technical evidence only. Keep fixture grants out of
normal progression demonstrations. Full conquest remains deferred and protected.

Use the existing root `Scripts/` workflow. Close the editor for native/reflected
changes. Run builds and engine probes sequentially; do not overlap other work
with a measured soak. Bootstrap `Validate` writes no packages; `Regenerate`
replaces generated assets explicitly. Keep hand-authored assets outside
`/Game/Lockdown/Generated/` and the separate web baseline unmodified.

Human usability, packaged dedicated-server delivery and remote Git/LFS transfer
are still open or conditional. Record actual v0.4 checks as Passed, Failed,
Not run or Conditional not available; prior v0.3 results are not v0.4 passes.
