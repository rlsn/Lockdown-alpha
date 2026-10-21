# Continuation — v0.4 B47 checkpoint closed

21 October 2026. The user requested wrap-up and movement to the next version,
with stress validation deferred. No critical issue remains in completed checks.
Start with the [B47 result](v04/B47_IMPLEMENTATION.md),
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[feature ledger](v04/FeatureLedger.csv) and [human review guide](v04/B47_REVIEW_GUIDE.md).
The [master plan](master/README.md) and [v0.4 specification](v04/README.md) remain
the design record. Next-version feature scope has not yet been supplied.

Runtime commit: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5`.
Cooked archive: `Artifacts/20261021-092725-233-PackageWin64/Archive/Windows`.
All 99 source/configuration hashes and seven payload hashes match its manifest.
The package was created from the preceding HEAD with these working changes;
subsequent wrap-up changes affect documentation only.

## Completed implementation and checks

B40–B46 supply the locked private class lobby, six attributes, learned/ranked
skills and four shared slots, held preparation, subclasses, books, Charter
Witness, persistent purses/theft, connected city/two-town region, bounded work
and XP, shared NPC healing, watch sabotage and earned Seal acquisition/recovery.
Normal creation grants no fixture currency, progression or Seal. Guild E remains
courier work; Shift+E selects four-second fabrication with Witness, four unused
credits and 100 gold, once per contender. An earned/recovered Seal uses the real
ten-second Heart channel while central defence stays protected.

B47 corrects sampling drift to retain 20 Hz authority/sight and 10 Hz publication,
fixes a variable-frame dash overshoot using Unreal's partial final movement tick,
and lets the wrapped HUD panel size to its content. Default graphics quality and
existing test thresholds remain unchanged.

Editor/Game builds, 28-asset validation and Win64 cook passed. Fifteen native
rules pass, including cadence and privacy allowlists. Final cooked checks pass
for both regional layouts, skills, preparation, theft, fabrication, waiting
withdrawal, ten resets, delayed owning clients, combat and restricted views.
Native variable-frame combat passes 48 checks at a 360.18 cm open dash, including
wall/ramp/ledge/tree collision. The B47 report indexes exact counts and artifacts.

Calibrated cooked graphics profiles retain default Epic/ray-tracing settings at
actual 1080p output. Near-100/150 ms measured echo conditions pass the unchanged
250 ms action-acknowledgement p95 target. Rendered median is about 60 FPS and
active-frame p95 about 30 ms; driver-reported over-budget use is zero. Automatic
internal screen percentage is retained. Short profiles do not establish long
rendered durability or a root cause for historical B45 GPU-budget warnings.

The ordinary 25-minute automated practice passed 37 checks in 1500.028 real
seconds at its original deadline. It earned Witness, three visits, eight paid
jobs, level five and one Seal with native travel and no grants or clock changes.
It ended with one no-acquisition result, 100 gold, four unused credits and one
unconsumed Seal. Actor range was 92–99; late memory spread 47.42 MiB. No kills,
deaths or Theft draws occurred. This is one headless automated contender.

## Deferred validation carried forward

The separate 90-minute technical soak was stopped at user direction after its
last 600.1-second audit and five successful forced lethal batches. No failure
was logged in that observed interval, but final percentile checks did not run.
It is not a 90-minute pass. Its deadline extension, real return timers, body/HP/
action/impact fixtures and partial evidence are explicit in
`Artifacts/20261021-100052-183-Packaged-V04-Soak-5400-150-25-Default/InterruptedByUser.json`.
All three owned test processes were stopped.

A fresh cold checkout of B47 was not run during wrap-up. The prior B46 cold
checkout remains historical evidence at its own source revision. Current warm
Editor/Game builds and cooked runtime checks passed. Remote Git/LFS delivery and
dedicated packaging remain conditional/unverified; authenticated resume is not
retained. Actual human multiplayer, contrasting builds, risk, counterplay,
usability and pacing remain unvalidated. The review guide supplies the original
six-match/two-session process; no human observations were invented.

The working ledger and C01–C48 table preserve these partial/deferred cases.
Proceed with the user's next-version scope and carry these validation gaps
forward explicitly; do not silently convert deferral into acceptance.

## Reproduction

Keep UE 5.8.3 CL 58210709. Run Unreal builds/probes sequentially with source stable.
BuildEditor.ps1 and BuildGame.ps1 accept optional MaxParallelActions 4 on this
16 GB machine. Asset Validate writes nothing; there are 28 generated packages.
Preserve the frozen specifications/templates/references, cook-safe navigation
envelope, authored assets and separate web baseline.

RunDevelopmentCheck.ps1 -Session selects the real ordinary 25-minute session.
-ReleaseAudit selects the labelled death/withdrawal fixture. Existing -Region,
-Preparation, -WorldChecks, -Skills, -Seal, -Network and -Reset selectors remain.
-PackageRoot chooses the archive. RunSoakCheck.ps1 -V04 -LifeCycles -Headless
-Seconds 5400 -RoundTripMs 150 -JitterMs 25 -OneWayBaseMs 50 is the deferred
technical profile. It extends only the deadline for that duration and explicitly
injects lethal load fixtures. It does not validate ordinary or human pacing.

Open the archive through Scripts/OpenPackaged.ps1 -Resolution 1080p for manual
Host/Join review. See B47 for actual hardware, quality, transport, counts and
failed intermediate results. Raw artifacts remain local and ignored by Git.
