# B42 — equipped skill execution

19 October 2026. This checkpoint implements the common execution boundary for
the nine catalog skills. It does not complete B43–B47 or the full v0.4 release.

## Runtime changes

Slots 1–4 resolve against the owning contender's current build revision on the
authority sample. The client sends a slot, never an arbitrary learned SkillId.
The same bounded queue arbitrates basic attacks and skills; Step takes priority
within a sample and may interrupt an existing action. Legacy action wrappers
cannot cast learned skills in the v0.4 profile. Q and Space resolve equipped
Ambush and Step slots. E's existing transaction protocol cannot encode a skill.

Every accepted skill starts its absolute cooldown in retained build state,
indexed by SkillId. Acceptance snapshots the source build and result. Equip/rank
plans retain cooldowns; an interrupted Ambush survives pawn destruction and a
new-life return with the same endpoint. Actor-local legacy cooldown fields are
not the authority for v0.4 skill availability.

Step uses the existing native root-motion source with a bounded scaled distance
over 200 ms. Collision, ramps and ledge blocking remain native. Ambush retains
the rear, quiet, target-history and warning rules, with scaled snapshot damage.
Spark locks the nearest visible legal living body, ignores wards, rechecks range,
life/protection and sight at impact, and retains recovery on a miss. Both casts
warn the target privately without sending a caster identity or position. Positive
early damage interrupts casts; simultaneously due damage remains in one batch.

Patch is a stationary, self-only held channel. Full HP rejects before cooldown.
Release, stale input after 250 ms, displacement, movement input, positive damage,
another accepted action, menu/focus clearing and death cancel it. Damage on its
deadline wins over healing. Combat key release does not cancel a committed cast.
Private active-skill state is cleared at death/return and when results stop play.
Results cannot leave a pending held channel visible or complete its heal.

Pickpocket, Parley, Analyze, Survey and Heave use the same held action slot and
source snapshot. Server-only target capabilities separate read-only validation,
accepted reservations and once-only completion. This permits B44 pair retry
reservations to survive cancellation. Range, geometric sight, source state and
target capability are checked again before completion. Pickpocket additionally
requires a living, unprotected, unexposed rear target within 100 cm. Static tasks
use 120 cm. No production task or wallet capability is registered yet.

A server-configured NPC build uses this same slot/revision/channel/cooldown path.
Untrained NPC requests fail. NPC build refresh cannot erase an existing cooldown.
The probe exercises NPC Patch; authoring the actual trained NPC remains B45.

The native HUD displays four mixed slots with rank, press/hold instructions and
remaining cooldown. Four transient Enhanced Input actions retain the original
saved migration input assets. Public observations still have exactly the eight
approved fields; source snapshots, private cooldowns and task callbacks are not
replicated. No class-specific body or remote canonical actor was added.

The saved catalog now validates all nine formulas, input modes, cooldowns and
channel/cast timings against the native schema-one contract. Twelve native rule
tests include rejected rehashed tuning changes and all six subclass arithmetic
effects. The unchanged saved catalog still uses the B41 source checksum.

## Verification boundary

`RunDevelopmentCheck.ps1 -Skills` uses explicit build grants, teleports, controlled
sample deadlines and mock task capabilities. Its utility commits demonstrate
execution and cancellation, not Charter Witnesses, wallet transfers, RNG draws or
real progression. The death check invokes the actual destruction/return path with
a controlled deadline. Network probes use real owning RPCs, transport shaping,
native movement and held refreshes. Their participants are automated clients.

Normal-profile class learning, preparation previews/commits, books and subclass
selection remain B43. Charter services, stable wallets, theft rolls, receipts,
pair retries and consequences remain B44. The connected region, trained NPC
content, sabotage and real rewards remain B45; earned fabrication remains B46.
No normal-profile fixture resources or free Seal were introduced.

## Executed evidence

Paths below are relative to repository-root `Artifacts/`. Engine: Unreal 5.8.3
CL 58210709. Results here are engineering evidence, not human acceptance.

| Check | Result | Evidence |
| --- | --- | --- |
| Editor build | Passed | `20261019-232054-017-BuildEditor/BuildEditor.log` |
| Native rules | Passed, 12 tests | `20261019-230334-409-Automation/Report/index.json` |
| Skill authority probe | Passed, 62 checks | `20261019-232145-153-Skills-DevelopmentCheck/Skills.json` |
| Two remote clients, configured 100 ms RTT / 25 ms jitter | Passed, Server 8 / A 5 / B 6 | `20261019-230554-906-Skills-DevelopmentNetwork-100-25/{Server,A,B}.json` |
| Two remote clients, configured 150 ms RTT / 25 ms jitter | Passed, Server 8 / A 5 / B 6 | `20261019-230654-099-Skills-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Retained legacy combat | Passed, 48 checks | `20261019-230751-848-CombatCheck/Combat.json` |
| Retained legacy objective scenarios | Passed, 53 / 3 / 3 / 4 / 5 checks | `20261019-230820-528-ObjectiveCheck/` |
| Native 720p / 1080p mixed hotbar | Captured and visually inspected; text fits | `20261019-230933-347-Skills-DevelopmentUI-720/SkillsHUD.png`; `20261019-231104-576-Skills-DevelopmentUI-1080/SkillsHUD.png` |
| Saved asset validation | Passed, 24 packages | `20261019-232226-716-Bootstrap-Validate/Bootstrap.log` |
| Win64 Game build/cook/stage/archive | Passed, Development | `20261019-232246-203-PackageWin64/{Package.log,PackageManifest.json}` |
| Cooked skill authority | Passed, 62 checks | `20261019-232521-320-Packaged-Skills-DevelopmentCheck/Skills.json` |
| Cooked two-client skills, configured 150 ms / 25 ms jitter | Passed, Server 8 / A 5 / B 6 | `20261019-232534-421-Packaged-Skills-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Cooked B41 character regression | Passed, 24 checks | `20261019-232757-650-Packaged-DevelopmentCheck/Local.json` |
| Cooked ten real world resets | Passed, 42 checks | `20261019-232627-788-Packaged-DevelopmentReset/Reset.json` |
| Cooked 720p mixed hotbar | Captured and visually inspected; text fits | `20261019-232817-253-Packaged-Skills-DevelopmentUI-720/SkillsHUD.png` |

The delayed-client cases check duplicate Spark commitment, a received pre-impact
warning, held Patch completion/cancellation, a 380 cm Step, rank-three Step blocked
by a wall and restricted client actor ownership. Packet delay is configured in
the native transport; echo measurements are in each client JSON report. Native
100 ms configuration measured median application echoes of 133.03 / 149.43 ms;
150 ms measured 199.40 / 183.01 ms. Cooked 150 ms measured 183.14 / 183.22 ms.
Native clients collected 55 samples each; the cooked clients collected 55 / 56. These are measured echoes including frame
scheduling, not a claim that configured delay equals application RTT exactly.

Runnable package: `Artifacts/20261019-232246-203-PackageWin64/Archive/Windows`.
Its manifest truthfully records source revision `07d8a71` with `sourceDirty=true`;
the source/payload hashes identify the actual B42 build. Clean-checkout
verification will be recorded after execution. The normal 25-minute match, separate 90-minute soak,
four-human capacity, six human matches, dedicated Server target and remote Git/LFS
delivery are not established by this checkpoint.

`Artifacts/B42DocumentationAndProvenance.json` verifies 75 source/config hashes,
seven packaged payload hashes, 66 Markdown files with no broken local links,
39 feature rows, 48 acceptance rows, four unchanged frozen snapshots and 23
checkpoint evidence paths.
