# Continuation — B44 Charter and wallet checkpoint

20 October 2026. B40–B44 are implemented and engineering-verified. The complete
v0.4 release remains in development. Start with the [B44 checkpoint](v04/B44_IMPLEMENTATION.md),
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[working feature ledger](v04/FeatureLedger.csv), [master revision 4](master/README.md)
and [v0.4 specification](v04/README.md). The checkpoint indexes executed evidence
and the current cooked archive. Previous B43 runtime: `f5da328`; its evidence
remains historical in [B43](v04/B43_IMPLEMENTATION.md).

## Current implementation

Frontend Host uses `?Profile=V04`. A private class/Ready lobby starts a locked
1–4 contender roster and a 1500-second deadline; one contender is practice.
Normal play starts without fixture gold, XP, Seal, book or Witness. Opening the
map without this option selects the explicit legacy regression profile.

The saved `DA_CharacterCatalog` validates native class/progression, nine skills,
books, preparation and Witness/theft tuning. Owner-only state carries attributes,
points, ranks, four slots, permanent subclass, revision and absolute cooldowns.
Death retains development and Witness, drops unread books, charges bounded gold
loss and returns a new protected body. Derived HP changes never heal.

Slots 1–4 and Q/Space aliases use the shared revision-checked skill queue.
Accepted actions snapshot the source. Basic attacks, Step, Ambush, Spark, Patch
and held utility share arbitration; damage wins over completion. P opens the
private ghost preview/library. Hold E two seconds at Guild or West after six
quiet seconds to commit an armed revision-checked preparation plan. Books cost
40 gold, have unique physical ownership and four unread spaces; three-second
stationary reads teach rank one without XP/SP/auto-equipment/cooldown reset.

B44 registers four Charter services around Guild: Heave, Analyze, Parley or
Survey at public threshold 12. An equipped valid hold grants one private permanent
Witness, no gold or XP. Own score/equipment/cooldown is previewed; insufficient
scores reject before cooldown. E remains basic work/loot.

All humanoids use private stable wallets. Residents start with 36; contenders
use actual gold. Death/return does not refill purses. Pickpocket acceptance
reserves its 20-second cooldown and stable 90-second pair retry. A valid
stationary one-second completion draws once from private Theft, using snapshot
source and current target WIS/Alert. Success atomically transfers at most 12
existing gold. Failure extends target Alert 30 seconds and source exposure four
seconds. Cancelled/invalid/replayed requests never reroll; outcomes/receipts and
victim tamper cues stay private. Retry/Alert/receipts survive body replacement.
No seed, wallet ID, target DC or purse value enters public observations.

## Next: B45

Implement the configurable connected region, authored routes and stations,
alternating-town work with eight paid receipts, first visits, sentinel and bounded
combat XP, authored trained NPC using the shared build/skill/wallet path, and real
equipped Analyze watchtower disruption. Separate paid-contract counts from
spendable credits. B44 task positions are temporary native public definitions;
move configuration with the region rather than duplicating reward paths.

Parameterize region, fog bounds/encoding and minimap together: proposed towns
are outside the current grid. Preserve the 64-entity observation bound. B43 found
no cooked AI route; ordinary player probes use swept clear-segment native
movement. Validate authored cooked NPC navigation explicitly in B45. Existing
courier rewards/geography do not establish the final eight-job budget.

B46 supplies earned atomic fabrication and normal zero-kill acquisition. Keep
normal conquest protected. B47 supplies complete release checks, 25-minute normal
play, separate 90-minute soak and human review. Do not call checkpoint engineering
tests human acceptance or full 60–90 minute pacing.

## Evidence and workflow

`Scripts/RunDevelopmentCheck.ps1 -WorldChecks` runs 56 authority cases;
`-WorldChecks -Normal` travels and holds the preset Rogue's Survey through native
input with no resource/build grants. `-WorldChecks -Network -RoundTripMs 150 -JitterMs 25`
uses real remote owners; `-WorldChecks -CaptureUI -Height 720` or `1080` checks
HUD/markers. `-PackageRoot` selects cooked execution. `-Preparation` retains 61
cases, `-Skills` 62, and no selector 24. `-Reset` performs 74 checks across ten
actual world reloads, dirtying actual theft draws, wallets, Witness, retry, Alert,
books and channels. Fourteen native rules cover catalog contracts, arithmetic and
unchanged eight-field observation/public-state allowlists. Injection is nonshipping,
authority-only and requires the explicit WorldCheck probe flag. Delayed theft setup
uses labelled grants/teleports and an authority-clock advance for the second retry.

Keep Unreal 5.8.3 CL 58210709 and focused C++. Close the editor for native/reflected
changes and keep sources stable during builds. Run Unreal builds/probes sequentially.
Bootstrap Validate writes no assets; Regenerate explicitly replaces generated
content. Preserve frozen specifications, blank templates, hand-authored content
outside `/Game/Lockdown/Generated/` and the separate web baseline.

Packaged networking uses a headless listen authority with a nonparticipating
diagnostic local connection, not a dedicated Server target. Fresh local checkout
verification reuses the installed engine/shared caches and locally hydrated LFS
objects; remote Git/LFS delivery remains unverified.
