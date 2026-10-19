# Continuation — B42 equipped execution checkpoint

19 October 2026. B40/B41 are complete. B42 equipped execution is implemented and
engineering-verified; see its [checkpoint record](v04/B42_IMPLEMENTATION.md).
The full v0.4 release remains in development. Read the
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[working feature ledger](v04/FeatureLedger.csv),
[master revision 4](master/README.md) and [v0.4 package](v04/README.md).

## Current implementation

The saved `DA_CharacterCatalog` imports the checked JSON companion and validates
all nine native skill formulas/timings. `FLDCharacterBuild` is retained private
contender state: class, six attributes, level, points, ranks, four slots, subclass,
build revision and absolute cooldown endpoints. DEX derives native speed; STR
snapshots basic damage. HP/damage preserve fractions; recalculation never heals.
Death retains the build and applies the new delay/15 percent loss, then returns a
new body/life at full derived HP with four-second protection.

Frontend Host uses `?Profile=V04`. Private class/Ready/host RPCs start a locked
1–4 contender roster; one is practice. The 1500-second timer starts then. Gameplay
is blocked in the lobby. Public state contains only profile/start/aggregate counts.
The explicit legacy profile opens the sandbox without the option for regressions.
Normal v0.4 has no fixture gold, XP, Seal, book or Witness.

Slots 1–4 resolve against the owner build revision at the authority sample.
Q/Space alias equipped Ambush/Step slots. A mixed hotbar shows rank, cooldown and
press/hold use. Raw legacy actions and E's existing protocol cannot bypass this.
`LDSkillExecution.cpp` supplies source snapshots, native scaled Step, retained
Ambush counterplay, locked single-body Spark, held self-Patch and utility dispatch.
Every accepted skill starts its cooldown by persistent contender and SkillId.

Held skills cancel on release/stale input, movement, damage, menu/focus clearing
or death. Combat key release does not cancel a committed cast. Damage wins over
held completion. Existing objective work cannot complete in parallel. Private
active-skill UI state clears at death/return/results. NPC builds use the same equipped
path; shared Patch is covered by a fixture, not authored NPC content yet.

Utility target capabilities have Validate/Accept/Commit stages and a request token.
Validate is read-only; Accept can reserve a retry window; completion rechecks
range/sight/source/target and uses the source snapshot. No production Charter or
wallet capability is registered. Mock callbacks prove dispatch, not rewards/RNG.
The public observation schema still contains exactly eight approved fields.

## Next: B43

Implement preparation with separate Learned/Equipped sections and a local ghost
preview. Apply arms a proposed `FLDBuildPlan`; a stationary two-second E hold at
an authorized station commits atomically after six quiet seconds. Recheck owner,
life, build revision, range, sight, budgets, release/stale input and source action
state at completion. Reject without spending; retain current absolute cooldowns.
Offer cumulative class-learning/rank costs and a permanent free level-four
subclass choice through this transaction. Unused points and empty slots are valid.

Implement real unique physical books: Guild sale for 40 gold, four unread capacity,
owned or dropped state, a stationary three-second read after six quiet seconds,
once-only consumption and rank-one teaching. Known skills reject with the book
retained. Reading does not auto-equip or upgrade. Death drops unread books and
retains knowledge. A new dropped-book observation kind must preserve the
approved public fields and receive filtering/receipt tests.

Demonstrate normal cross-class book learning and two different builds from one
start. Test overspend/stale/duplicate/interrupted commits and a recently used
skill remaining on cooldown after swaps/rank changes. Preserve B42's boundary for
every new contextual entry point. Use existing public sites until the B45 region;
keep test grants explicitly gated outside normal play.

B44 still requires real Charter services, stable wallets, pair retries, the RNG
stream, receipts and theft consequences. B45 adds region/rewards/actual trained
NPC/sabotage; B46 adds earned fabrication. For B45, replace fixed site/district
helpers and parameterize fog bounds, encoding limits and minimap mapping together.
The proposed towns are outside the current grid. Separate paid-contract counts
from spendable credits. Full conquest remains deferred and protected.

## Evidence and workflow

The B42 report indexes the current package and executed evidence. There are still
24 saved packages; migration input assets were not overwritten. Twelve native
rules, 62 authority skill checks and two real remote clients at configured
100/150 ms RTT with jitter pass. Retained combat/objective checks and reviewed
720p/1080p mixed-hotbar captures pass. B41 results remain historical.

Use `Scripts/RunDevelopmentCheck.ps1 -Skills`, adding `-Network -RoundTripMs 150
-JitterMs 25` for delayed skill checks or `-CaptureUI -Height 720` / `1080` for the
mixed hotbar. `-PackageRoot` selects a cooked build. Without `-Skills`, the retained
B41 local/lobby/network/reset checks remain available. Packaged network tests use
a labelled headless listen authority and nonparticipating diagnostic connection.
They do not certify a dedicated Server target or four-human capacity.

Keep Unreal 5.8.3 CL 58210709 and focused C++. Close the editor for native/reflected
changes; keep sources stable during builds. Run builds/probes sequentially and
keep measured soaks separate. Bootstrap Validate writes no assets; Regenerate
replaces generated content explicitly. Preserve the separate web baseline and
hand-authored assets outside `/Game/Lockdown/Generated/`.

Human usability, normal 25-minute play, a separate 90-minute soak, six human
matches, dedicated packaging, authenticated resume and remote Git/LFS delivery
remain pending/conditional. These checkpoint tests do not establish full release
acceptance or 60–90 minute human pacing.
