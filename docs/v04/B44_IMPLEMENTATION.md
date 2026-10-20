# B44 — Charter checks and persistent wallet theft

20 October 2026. This checkpoint adds production Charter alternatives and the
bounded theft transaction to the retained migration map. B45–B47 and complete
v0.4 release acceptance remain separate work.

## Runtime changes

Four visible services surround Guild: heavy goods (Heave), civic mechanism
(Analyze), negotiation (Parley) and field report (Survey). Each checks its equipped
skill against public threshold 12 through the shared held skill path. Insufficient
scores reject before cooldown. A valid completion grants one permanent private
Charter Witness receipt, with no gold or XP. Further services cannot duplicate it.
Filtered task proxies show public names; the HUD previews own score, equipment,
cooldown and hold instructions. E continues to perform basic work/loot.

Humanoids share a server wallet capability. Contenders use their actual gold and
stable contender identity. Ordinary residents start with 36 gold; combat death
and return carry the original wallet ID into the new body. Neither replacement
bodies nor slot changes replenish purses or clear pair retries. Trained NPCs use
the same equipped ability and wallet path.

Pickpocket requires living unprotected source/target, the equipped learned skill,
stationary source, 100 cm, body sight, rear bearing and an unexposed target. The
accepted one-second channel reserves a 90-second source/target wallet retry and
starts its 20-second skill cooldown. Cancellation and failed final legality draw
nothing and retain both deadlines. Exactly at the applicable deadline is allowed.

At valid completion, the private Theft stream draws once, using the accepted
source build and current target WIS/Alert. Success atomically transfers at most
12 existing gold, including zero for an empty purse. Failure transfers nothing,
extends target Alert by 30 seconds and source exposure by four seconds. Victims
receive private purse/tampering feedback with no attacker coordinate. Theft
awards no XP. Outcome/amount/receipt are private to the source; target resistance,
wallet identity, balance and private seed never enter observation payloads.

Source and target geometry/life are rechecked after the combat damage batch.
Accepted reservations and bounded receipts survive body replacement. Request
replay returns its cached acknowledgement without another random draw or effect.
Cosmetic randomness and identity generation cannot advance the private stream.
The saved catalog validates the Witness/theft contract against native schema one.
The observation schema still has exactly eight fields; Charter adds a public
kind and uses its existing presentation index for the four public alternatives.

## Verification boundary

Authority tests use labelled grants, teleports, controlled sample clocks and
fixed diagnostic rolls/balances. Injection is nonshipping and requires the
explicit WorldCheck probe flag; ordinary clients have no injection RPC or seed.
Delayed network cases use real remote owners, with labelled setup gold, positions
and authority clock advancement to reach the second pair deadline. An ordinary
practice case instead travels with native movement and earns a Witness through
the starting Rogue's actual equipped Survey, without resource/build grants.

The four temporary service locations are native public definitions on the old
map. B45 still supplies the connected region, configurable graph, bounded work,
exploration/sentinel rewards, authored trained NPC and real sabotage. Its cooked
NPC navigation check remains necessary after B43 found no AI route in the
migration package. B46 supplies earned fabrication. No free Seal or normal-profile
test resources were added.

## Executed evidence

Paths below are relative to repository-root `Artifacts/`. Engine: Unreal 5.8.3
CL 58210709. All listed runs completed without failed checks.

| Check | Result | Evidence |
| --- | --- | --- |
| Final Editor build | Passed | `20261020-113928-599-BuildEditor/BuildEditor.log` |
| Saved assets | Validate passed; 24 packages | `20261020-113941-756-Bootstrap-Validate/Bootstrap.log` |
| Game build/cook/stage/archive | Win64 Development passed | `20261020-113958-922-PackageWin64/Package.log` |
| Native rules | 14 passed | `20261020-113013-136-Automation/Report/index.json` |
| Native world transactions | 56 passed | `20261020-112955-531-WorldChecks-DevelopmentCheck/WorldChecks.json` |
| Native ordinary input | 7 passed; 1409.58 cm native travel | `20261020-113047-876-WorldChecks-Normal/Normal.json` |
| Native delayed clients, 100 ms / ±25 ms per leg | Server 7 / A 6 / B 6 passed | `20261020-113508-335-WorldChecks-DevelopmentNetwork-100-25/{Server,A,B}.json` |
| Native delayed clients, 150 ms / ±25 ms per leg | Server 7 / A 6 / B 6 passed | `20261020-113545-831-WorldChecks-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Retained native skills / preparation | 62 / 61 passed | `20261020-113713-858-Skills-DevelopmentCheck/Skills.json`; `20261020-113727-219-Preparation-DevelopmentCheck/Preparation.json` |
| Native ten-world reset | 74 passed | `20261020-113741-028-DevelopmentReset/Reset.json` |
| Cooked world transactions | 56 passed | `20261020-114316-672-Packaged-WorldChecks-DevelopmentCheck/WorldChecks.json` |
| Cooked ordinary input | 7 passed; 1109.09 cm native travel | `20261020-114320-227-Packaged-WorldChecks-Normal/Normal.json` |
| Cooked delayed clients, 150 ms / ±25 ms per leg | Server 7 / A 6 / B 6 passed | `20261020-114335-107-Packaged-WorldChecks-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Retained cooked skills / preparation | 62 / 61 passed | `20261020-114400-109-Packaged-Skills-DevelopmentCheck/Skills.json`; `20261020-114402-706-Packaged-Preparation-DevelopmentCheck/Preparation.json` |
| Cooked ten-world reset | 74 passed | `20261020-114405-313-Packaged-DevelopmentReset/Reset.json` |
| Final HUD and public markers | Cooked 720p / native 1080p captured and visually inspected | `20261020-114416-085-Packaged-WorldChecks-DevelopmentUI-720/WorldChecks.png`; `20261020-114425-874-WorldChecks-DevelopmentUI-1080/WorldChecks.png` |
| Retained development / combat | 24 / 48 passed | `20261020-114450-962-DevelopmentCheck/Local.json`; `20261020-114504-839-CombatCheck/Combat.json` |
| Retained objective scenarios | Main 53; exact deadline 3; late claim 3; no claim 4; withdrawal 5 passed | `20261020-114518-439-ObjectiveCheck/{Main,ExactDeadline,LateClaim,NoClaim,Withdraw}.json` |

Current archive: `Artifacts/20261020-113958-922-PackageWin64/Archive/Windows`.
Use its `Lockdown.exe`, Host, select a class, Ready and Start. Its manifest records
the precommit revision and dirty flag truthfully, alongside source/config and
archived payload SHA-256 values. The region and earned Seal route are still absent.
All 86 current source/config files and seven archived payloads match the manifest;
68 Markdown pages, all local links, 39 feature rows, 48 acceptance rows and four
frozen predecessor snapshots pass the documentation/provenance check in
`Artifacts/B44DocumentationAndProvenance.json`.

The cooked remote echo medians were 199.74 / 203.61 ms from 81 / 80 samples.
These verify emulated native transport, not WAN latency or an action-acknowledgement
percentile. The authority is a headless listen world with a nonparticipating local
connection and two remote owners; a packaged dedicated Server target and
four-human capacity remain unverified. The ordinary input case is automated
practice, not a human match or an earned objective-route demonstration.

Intermediate failed runs remain in ignored Artifacts and do not count as passes.
The first network fixture selected the Scholar's Spark slot instead of Analyze;
the corrected setup uses slot two for both Scholar and Rogue. Initial include-order
build failures were repaired before the successful builds. The first HUD capture
showed weak world-label contrast; the final build darkens those public labels.
Screenshots establish engineering layout inspection, not human usability.

Human matches/usability/balance, full ordinary 25-minute play, the separate
90-minute soak, four-human capacity, dedicated packaging and remote Git/LFS
delivery remain pending/conditional. This checkpoint does not establish those
release gates or 60–90 minute pacing.
