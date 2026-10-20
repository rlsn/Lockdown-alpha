# B45 — Connected progression region

20 October 2026. B45 is implemented and engineering-verified.
B46 earned fabrication and B47 full-release acceptance remain separate work.

## Region and presentation

The frontend now hosts `L_DevelopmentRegion` with the v0.4 profile. Its saved
`DA_CivicRegion` defines a 12000 × 8000 cm city/two-town/wilderness region,
public and covered town routes, an outer-to-outer path, separated city spawns,
Guild book market, three preparation stations and eight peaceful residents.
The migration map and its explicit historical regression profile are retained.

`DA_AlternateRegion` / `L_RegionAlternate` use five nodes, different identities,
node order, route edges and a 15000 × 10000 cm envelope. Runtime uses region IDs
and configured edges, not district array indices. Generated maps save navigation
data and the bootstrap checks complete town-to-town navigation before saving.
Validate compares saved definitions, geometry, route markings and navigation
bounds against `Config/RegionLayout.json` without rewriting content.
The region navigation volume includes a noncolliding runtime box, preserving its
full envelope after BSP authoring data is stripped during cooking.

The existing 60 × 40 mask and 64-observation bound remain fixed. Each complete
view carries validated public grid origin/cell size; authority sight, pose wire
validation, fog material scale/offset and minimap projection use that grid.
Public landmarks include both towns, the wilderness visit marker and watch
control. Filtered control and visibly hostile sentinel presentations add no
private fields to the eight-field entity schema. Public objective state adds
only the watch's offline/lockout endpoints; private builds and rewards stay owned.

## Progression and world transactions

Each contender gets at most eight paid courier deliveries. The server seeds its
first town, alternates subsequent paid destinations and retains the active
destination through unrelated events. Assigned-town collection and Guild
delivery each require the existing three-second stationary hold. Delivery pays
25 gold, 45 XP and one unused commission credit once, then imposes the existing
20-second acceptance delay. Paid count and unused credits are separate fields;
death cancels the active contract and retains both budgets.

Three first visits award 25 XP each through consecutive, eligible ground
traversal across the configured trigger boundary. Camera/ward coverage,
teleports, protection, initial placement and body return establish a baseline
without rewards. Region-ID receipts survive lives. Eight deliveries plus three
visits provide 435 XP before the existing 420/level-five cap.

The authored trained resident knows/equips rank-one Patch. Injury stops its
navigation and invokes the same revision, slot, hold, snapshot, interruption and
absolute cooldown path used by contenders. Its build/cooldowns and stable wallet
carry through resident return. Other residents retain nonviolent local routines.

The marked hostile sentinel uses 100 HP, twelve-damage melee, 96 cm reach,
300 ms windup and 1200 ms start cadence. It acquires visible living unprotected
contenders within 500 cm of origin, leashes at 600 cm, never targets residents
and resets living HP after six seconds without a legal target. Defeat returns it
after 60 seconds. On the lethal authority batch, greatest direct damage wins
credit; opaque body identity breaks ties. A dead credited finisher gets nothing.
Each contender can receive two once-only rewards of 45 XP and 25 gold per match.
Its wallet identity persists across return. Residents and PvP still pay nothing.

Equipped Analyze at public score 14 can hold two seconds at the actual watch
control. Successful commit disables shared sight for 30 seconds, locks the
control globally for 60 seconds and exposes the actor four seconds. Damage,
release, stale input and lost legality cancel through the shared skill path;
losing a race has no effect and keeps the accepted normal cooldown. The source
is removed in the next resulting view and restored at its fixed endpoint.
Grand Heart defence flags remain protected.

## Verification boundary

Authority/network probes label resource grants, build setup, teleports,
controlled sample times and adversarial lethal-batch fixtures. Ordinary automated
practice instead uses native player movement on actual navigation paths, the
starting Rogue's Survey, original timers, three visits and eight paid jobs. Its
nonlethal strike lets the authored NPC demonstrate autonomous healing; it does
not grant XP or count as a kill. This is automated practice, not human acceptance.

## Executed evidence

Paths below are relative to repository-root `Artifacts/`. Engine: Unreal 5.8.3
CL 58210709. Listed test reports have zero failed checks.

| Check | Result | Evidence |
| --- | --- | --- |
| Final Editor build | Passed | `20261020-185740-446-BuildEditor/BuildEditor.log` |
| Region-only map rebuild | Passed; other generated content retained | `20261020-183146-432-Bootstrap-Generate/Bootstrap.log` |
| Saved assets | Validate passed; 28 packages | `20261020-183235-163-Bootstrap-Validate/Bootstrap.log` |
| Final game build/cook/stage/archive | Win64 Development passed | `20261020-185834-018-PackageWin64/Package.log` |
| Native rules | 15 passed | `20261020-180825-339-Automation/Report/index.json` |
| Final native region authority | 93 passed | `20261020-183218-509-Region-DevelopmentCheck/Region.json` |
| Native alternate authority | 93 passed before cook-bounds repair | `20261020-182157-338-Region-DevelopmentCheck-Alternate/Region.json` |
| Native ordinary input | 30 passed; 83326.14 cm actual travel | `20261020-180953-733-Region-Normal/Normal.json` |
| Native delayed clients, 150 ms / ±25 ms per leg | Server 10 / A 6 / B 6 passed | `20261020-181759-360-Region-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Final native delayed clients, 100 ms / ±25 ms per leg | Server 10 / A 6 / B 6 passed | `20261020-184916-321-Region-DevelopmentNetwork-100-25/{Server,A,B}.json` |
| Native ten-world reset | 95 passed | `20261020-181841-116-Region-DevelopmentReset/Reset.json` |
| Retained native theft / preparation / skills | 56 / 61 / 62 passed | `20261020-182214-819-WorldChecks-DevelopmentCheck/WorldChecks.json`; `20261020-182229-712-Preparation-DevelopmentCheck/Preparation.json`; `20261020-182244-088-Skills-DevelopmentCheck/Skills.json` |
| Cooked main / alternate authority | 93 / 93 passed, including actual navigation | `20261020-183734-668-Packaged-Region-DevelopmentCheck/Region.json`; `20261020-183739-773-Packaged-Region-DevelopmentCheck-Alternate/Region.json` |
| Refreshed archive main / alternate authority | 93 / 93 passed | `20261020-190055-399-Packaged-Region-DevelopmentCheck/Region.json`; `20261020-190100-428-Packaged-Region-DevelopmentCheck-Alternate/Region.json` |
| Cooked delayed clients, 150 ms / ±25 ms per leg | Server 10 / A 6 / B 6 passed | `20261020-183743-425-Packaged-Region-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Cooked ten-world reset | 95 passed | `20261020-183812-214-Packaged-Region-DevelopmentReset/Reset.json` |
| Final cooked ordinary input | 30 passed; 83848.01 cm actual travel | `20261020-183843-660-Packaged-Region-Normal/Normal.json` |
| Cooked HUD / minimap / markers | 720p and 1080p captured and inspected; GPU-budget warning observed | `20261020-183823-038-Packaged-Region-DevelopmentUI-720/Region.png`; `20261020-183833-867-Packaged-Region-DevelopmentUI-1080/Region.png` |
| Retained native combat | 48 passed | `20261020-184636-288-CombatCheck/Combat.json` |
| Retained native development | 24 passed | `20261020-184958-700-DevelopmentCheck/Local.json` |
| Retained objective scenarios | Main 53; exact deadline 3; late claim 3; no claim 4; withdrawal 5 passed | `20261020-184658-954-ObjectiveCheck/{Main,ExactDeadline,LateClaim,NoClaim,Withdraw}.json` |

Current archive: `Artifacts/20261020-185834-018-PackageWin64/Archive/Windows`.
Open its `Lockdown.exe`, Host, choose class, Ready and Start. The archive manifest
records its real precommit source revision/dirty state and source/config/payload
hashes. Native ordinary/network/reset runs preceded the cook-bounds repair; their
progression logic is unchanged, and final cooked checks verify the repaired maps.
The last change guards the editor commandlet's contradictory Validate/rebuild
flags. All seven runtime payload hashes are identical between the tested
`183251-518` archive and the refreshed `185834-018` archive; exact paths/hashes
are checked in `Artifacts/B45PayloadEquivalence.json`. Its main/alternate authority
cases were also relaunched. The ordinary/network/reset/render evidence therefore
applies to the same runtime bytes in the current archive.
The cooked echo medians were 202.13 / 202.41 ms from 100 samples per owner.
This verifies emulated native transport, not WAN conditions or a measured action
acknowledgement percentile. Authority is a headless listen world with a
nonparticipating local connection and two remote owners, not a dedicated Server
target or a four-human capacity measurement.

## Repairs and remaining limits

The first map-authoring build was blocked by the engine's asynchronous-loading
navigation lock. The builder now finishes loading/asset compilation and releases
that lock before building navigation. The first cooked archive then exposed the
older migration problem: serialized navigation expected 351 tiles, while the
cooked BSP volume described only three. `ALDRegionNavBounds` adds an ordinary
runtime box component; the repaired cooked world reports the full −6000…6000 /
−4000…4000 envelope and passes all route queries. Only the two owned region maps
were rebuilt. The first archive and failed runs remain ignored intermediate
artifacts and do not count as passing delivery evidence.

The book panel's old Guild-distance check also needed the regional site helper.
All three stations now exercise actual preparation, and the relocated market
executes the real forty-gold purchase. Initial compiler issues in the new map
builder were repaired before successful builds.

The native 720p capture showed a 23.11 MB GPU-budget warning; the final cooked
720p capture showed 54.50 MB over budget on the 8 GB RTX 4060 Laptop GPU. A warning
is also visible in the cooked 1080p capture. No rendering/scalability overrides
hide it. Layout is readable, but these are not rendering-performance passes.
Graphics memory/profile work remains an explicit B47 delivery limitation before
human acceptance; headless functional tests do not resolve it.

Complete earned acquisition remains B46. Normal 25-minute
play, the separate 90-minute soak, four-human capacity, dedicated packaging,
remote Git/LFS delivery and human strategy/usability review remain pending or
conditional. This checkpoint does not establish 60–90 minute pacing.
