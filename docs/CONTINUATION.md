# Continuation — B43 preparation and book checkpoint

20 October 2026. B40–B43 are implemented and engineering-verified. The full v0.4
release remains in development. Read the [B43 checkpoint](v04/B43_IMPLEMENTATION.md),
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[working feature ledger](v04/FeatureLedger.csv),
[master revision 4](master/README.md) and [v0.4 package](v04/README.md).
The checkpoint report indexes the current build, package and executed evidence.
Runtime checkpoint: `f5da328`. Its fresh local checkout passed cold Editor/Game
builds, 24-asset validation and 13 rules and stayed clean:
`Artifacts/20261020-102145-187-CleanCheckout/Result.json`.

## Current implementation

The saved `DA_CharacterCatalog` validates the native class/progression, nine skill,
book and preparation contracts. Retained private build state contains class, six
attributes, level, points, learned ranks, four slots, subclass, revision and
absolute cooldown endpoints. Derived HP changes never heal. Death retains the
build and knowledge, drops unread books, charges the bounded gold loss, then
returns a new body/life at full derived HP with four-second protection.

Frontend Host uses `?Profile=V04`. Private class/Ready/host RPCs start a locked
1–4 contender roster; one is practice. The 1500-second deadline starts then.
Gameplay is blocked in the lobby and during protection. Normal v0.4 has no fixture
gold, XP, Seal, book or Witness. Opening the map without this option selects the
explicit legacy profile used by retained regressions.

B42 slots 1–4 and Q/Space aliases resolve equipped skills against the current
build revision. Accepted actions snapshot the source and persist their SkillId
cooldown. Basic attacks, Step, Ambush, Spark, held Patch and utility capabilities
share arbitration. Damage wins over held completion. Raw/context bypasses reject.
Utility capabilities have read-only Validate, reservation Accept and once-only
Commit stages. No production Charter or wallet capabilities are registered yet.

P opens the private B43 panel. Local edits show a ghost preview; Apply only arms.
Hold E for two seconds at Guild or West within 120 cm and geometric sight after
six quiet seconds to commit the complete expected-revision plan atomically.
Release/focus loss, stale input, movement, damage, death, source changes or lost
range/sight cancel. Class learning/rank costs are cumulative; unused points and
empty slots are valid. Level-four subclass choice is free, class-bound and
permanent, with no extra skill or slot. The library scrolls independently so the
confirmation controls remain visible.

Guild sells Step/Spark/Patch/Parley books for 40 gold with four unread spaces.
Physical instances carry unique IDs, taught skill and owned/dropped state. Read
anywhere for three stationary seconds after six quiet seconds: consume once,
teach rank one, no SP/XP/auto-equip/upgrade/cooldown reset. Known books are retained.
Death/withdrawal drops unread books; opaque filtered book observations support
current visibility/range/sight/capacity-checked pickups. Public proxies reveal no
book skill, class, inventory or build. Observations retain exactly eight fields.

## Next: B44

Implement the four actual Charter alternatives using the shared equipped utility
capability path and public task thresholds, with one retained Witness receipt.
Show own scores without leaking a secret target DC. Register persistent humanoid
wallet capabilities independent of body replacement and NPC/human identity.

Implement Pickpocket rear/range/sight/protection/exposure eligibility, one server
d20 draw on valid completion, atomic transfer of at most 12 existing gold,
bounded receipts and stable 90-second source/target retry windows alongside the
20-second SkillId cooldown. Accept should reserve the pair window; cancelled,
invalid or duplicate requests must not draw or reroll. Failure applies 30-second
Alert and four-second exposure without leaking an unauthorized origin. Test
concurrent depletion, empty purses, life changes and role-neutral validation.

B45 still adds the connected region, bounded paid jobs/visits/sentinel rewards,
trained NPC and real sabotage. Existing courier rewards and geography are preview
behavior; the ordinary B43 demonstration does not certify the final eight-job
budget. Parameterize region, fog bounds/encoding and minimap together; proposed
towns are outside the current grid. The current 64-entity observation bound remains.
The cooked ordinary-run probe found no AI route and uses swept clear-segment
steering with native player movement; revalidate cooked NPC navigation in B45.
B46 adds earned fabrication. Keep normal conquest protected and future routes
accurately deferred. Separate paid-contract counts from spendable credits.

## Evidence and workflow

Use `Scripts/RunDevelopmentCheck.ps1 -Preparation` for authority transactions,
`-Preparation -Normal` for native travel and the courier-funded demonstration,
`-Preparation -Network -RoundTripMs 150 -JitterMs 25` for real remote owning RPCs,
and `-Preparation -CaptureUI -Height 720` / `1080` for the panel. `-PackageRoot`
selects a cooked archive. `-Skills` retains the 62 B42 cases; no selector retains
the 24 B41 cases. `-Reset` now dirties and clears books/channels as well as builds
across ten real world reloads. Thirteen native rules include bounded draft wire
serialization and the unchanged eight-field observation/public-state allowlists.

Keep Unreal 5.8.3 CL 58210709 and focused C++. Close the editor for native/reflected
changes; keep sources stable during builds. Run Unreal builds/probes sequentially.
Bootstrap Validate writes no assets; Regenerate explicitly replaces generated
content. Preserve frozen specifications, blank templates, hand-authored assets
outside `/Game/Lockdown/Generated/` and the separate web baseline.

Engineering probes label grants, teleports, controlled clocks and adversarial
fixtures. The ordinary B43 run uses real rewards/native travel/original timers;
it is automated local practice, not human acceptance. Packaged network checks use
a headless listen authority with a nonparticipating diagnostic local connection,
not a dedicated Server target. Fresh local checkout verification reuses the
installed engine/shared caches and locally hydrated LFS objects.

Human usability/diversity, normal 25-minute play, a separate 90-minute soak,
six human matches, four-human capacity, dedicated packaging, authenticated resume
and remote Git/LFS delivery remain pending/conditional. These checkpoints do not
establish full v0.4 acceptance or 60–90 minute human pacing.
