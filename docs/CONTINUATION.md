# Continuation — B45 region and progression checkpoint

20 October 2026. B40–B45 are implemented and engineering-verified. Start with the
[B45 checkpoint](v04/B45_IMPLEMENTATION.md),
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[feature ledger](v04/FeatureLedger.csv), [master revision 4](master/README.md) and
[v0.4 specification](v04/README.md). Full v0.4 acceptance remains in development.
Previous B44 runtime: `6356af1`; its historical evidence is in
[B44](v04/B44_IMPLEMENTATION.md).

## Current implementation

Frontend Host launches `L_DevelopmentRegion`. Opening either regional map also
selects v0.4 automatically. The private class/Ready lobby locks 1–4 contenders and
starts a 1500-second deadline; one is practice. There are no normal fixture gold,
XP, books, Witness or Seal grants. The migration map remains a diagnostic surface;
opening it without `?Profile=V04` selects the historical legacy profile.

The saved main region has City, two towns and WildCrossing, with public/covered
routes and an outer-to-outer path. The five-node alternate changes identities,
node order, edges and bounds. Both use saved public definition assets; runtime
never reads Config JSON. Fog origin/cell size travel in each bounded view, so
sight, wire pose validation, fog and minimap use the same envelope. Masks remain
60 × 40 and entity capacity 64. The observed entity allowlist remains eight fields;
public objective state adds only watch offline/lockout endpoints.

Retained private builds include class, attributes, points, learned ranks, four
slots, permanent subclass, revision and absolute cooldowns. Slots/Q/Space share
execution, snapshots and damage-before-completion arbitration. P opens private
preparation/books. Guild and both town stations commit armed plans after a
stationary two-second hold and six quiet seconds. Guild books cost 40, have unique
ownership and four unread spaces; three-second reading teaches rank one without
XP/SP/auto-equipment/cooldown reset. Death retains development/Witness, drops
unread books, charges bounded gold loss and returns a protected city body.

Four equipped Charter alternatives surround Guild at public 12; one completion
grants a permanent private Witness, no XP/gold. Stable humanoid wallets retain
balances through body return. Accepted Pickpocket reserves a 20-second skill
cooldown and 90-second pair retry; a valid stationary one-second completion draws
once from private Theft. Transfer is at most twelve existing gold. Failure extends
Alert 30 seconds and exposure four seconds. Cancellation/replay never rerolls.

Regional courier work assigns a seeded first town, then alternates destinations.
Collection and delivery each hold three seconds; delivery pays 25 gold, 45 XP and
one credit, with a 20-second acceptance delay. Eight paid deliveries is a retained
match budget separate from spendable commission credits. Death cancels the active
job and preserves both counts. Three first visits award 25 XP each through actual
eligible ground traversal; camera/ward/placement/protection/respawn cannot pay.

Eight peaceful residents use local navigation. One authored resident autonomously
uses actual equipped rank-one Patch when injured. Build/cooldowns and wallet carry
through its return. A visibly hostile WildCrossing sentinel uses 100 HP, 12 damage,
96 cm melee, 300 ms windup and 1200 ms cadence; it acquires visible unprotected
contenders within 500 cm, leashes at 600, resets HP after six quiet targetless
seconds and returns after 60 seconds. Greatest direct damage on the lethal batch
wins credit, with opaque-ID ties and no payout to a dead finisher. Each contender
gets at most two rewards of 45 XP/25 gold. Its wallet also survives return.

Actual equipped Analyze at public 14 holds two seconds at the watch control to
disable public sight for 30 seconds, apply a global 60-second lockout and expose
the source four seconds. Accepted cancellations/race losses retain skill cooldown.
The first resulting view removes the source; restoration uses its fixed endpoint.
Heart defence flags remain protected.

## Next: B46

Implement earned atomic Seal fabrication at Guild: retained Witness, four unused
credits, 100 gold, empty Seal carry slot and one craft per contender per match.
Use the existing stationary held objective transaction, with four-second duration,
final revalidation and damage-first interruption. Create the unique item before
charging so creation failure cannot spend resources. Witness and paid-job budget
remain separate from consumed credits. Make the Guild's work/fabrication choice
clear when both are available.

Keep Seal ownership/drop/recovery independent of the crafter's Witness. Verify a
zero-kill normal contender earns four jobs, crafts and claims through the real
10-second Heart path while central defences remain protected; another contender
must recover an earned dropped Seal. No fixture grant counts as earned acquisition.
Retain fixed deadline ordering, 240 cm contest and one terminal receipt.

B47 remains full release engineering/human review, ordinary 25-minute play and a
separate 90-minute technical soak. Four-human capacity, dedicated packaging,
remote Git/LFS delivery, human strategy/usability and full 60–90 minute pacing
remain pending/conditional.
Native and cooked HUD captures show GPU-budget pressure on the 8 GB RTX 4060
Laptop GPU. Rendering/profile work remains a B47 gate; functional headless passes
do not establish graphics performance. Preserve this finding before human review.

## Evidence and workflow

`Scripts/RunDevelopmentCheck.ps1 -Region` checks 93 authority cases;
`-Region -Alternate` uses the five-node layout. `-Region -Normal` uses native
navigation/movement and original timers for Witness, three visits, autonomous NPC
healing and eight jobs, without grants. `-Region -Network -RoundTripMs 150 -JitterMs 25`
uses real remote owners, races sabotage, delivers jobs and tests lethal-batch ties.
`-Region -Reset` performs 95 checks across ten real reloads. `-Region -CaptureUI
-Height 720` / `1080` captures the HUD. `-PackageRoot` selects cooked execution.

Retained selectors remain `-WorldChecks` (56), `-Preparation` (61), `-Skills` (62),
no selector (24) and legacy `-Reset` (74). Fifteen native rules include region
contracts and bounded grid serialization. Authority/network grants, teleports,
build changes and deadline advances are explicitly labelled engineering fixtures.
Ordinary automated practice is not human acceptance or a full-duration run.

Keep Unreal 5.8.3 CL 58210709. Close the editor for native/reflected changes and
keep source stable during builds. Run Unreal builds/probes sequentially. Bootstrap
Validate writes nothing; `-RebuildRegionMaps` rebuilds just the two owned region
maps from validated saved definitions. Navigation uses a cook-safe noncolliding
box envelope as well as editor BSP bounds. Preserve frozen specifications, blank
templates, hand-authored assets outside `/Game/Lockdown/Generated/` and the separate
web baseline. Packaged networking is headless listen authority plus two remote
owners, not a dedicated target. Fresh local clones reuse installed engine/shared
caches and local LFS objects; they do not establish remote delivery.
