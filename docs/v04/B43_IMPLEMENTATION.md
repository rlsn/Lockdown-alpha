# B43 — preparation, books and branching

20 October 2026. This checkpoint adds the preparation and book lifecycle on the
existing migration map. B44–B47 and full v0.4 acceptance remain separate work.
Runtime checkpoint: `f5da328ea8fe3738f7a1919e5ca58d3a350fb88e`.

## Runtime changes

P opens a private character panel with separate learned-library and four-slot
equipment sections, attribute and rank spending, subclass choice, unread inventory
and Guild purchases. A local ghost preview shows maximum/current HP, basic damage,
walk/sprint speed, own skill values and remaining points. Editing or Apply alone
changes no authority state. Apply arms the draft; a stationary two-second E hold
at Guild or West station commits it atomically after six seconds without positive
damage dealt or taken. Release, focus loss, stale input, movement, displacement,
damage, death, changed revision or lost range/sight cancel the transaction.

Class learning requires level two and its class list. Rank one-to-two costs one
SP and two-to-three costs two; cumulative changes pay every step. Empty slots and
unused points are valid; duplicate or unknown equipment rejects. A free level-four
subclass choice is class-bound and permanent, with no free skill or extra slot.
Recalculation preserves current HP except for the existing maximum clamp.

The Guild sells Step, Spark, Patch and Parley for 40 gold each, with four unread
spaces per owner. Each physical book has a unique identity and either an owner or
a dropped position. A stationary three-second read anywhere after six quiet
seconds consumes an owned book once and teaches rank one without spending SP,
equipping, activating, upgrading or granting XP. Known-skill books are retained.
Death/withdrawal drops unread books and keeps learned knowledge. Pickups resolve
an authorized opaque observation ID, then recheck current visibility, geometry,
range and capacity. Public book proxies are generic; they carry no skill identity.

The transaction director runs inside the combat sample after damage and before
objective completion. Preparation and reading occupy the common action slot;
Step and basic attacks interrupt them. Request receipts, life/match guards, a
bounded request queue and rate limit prevent repeated deductions or consumption.
Draft RPC serialization uses fixed-size arrays on the wire. Owner-only inventory
and build state never enter the eight-field observation or public state schema.
Accepted absolute skill cooldowns survive reading, rank changes and preparation.

## Verification boundary

The authority transaction probe uses explicitly labelled XP/gold grants,
teleports, controlled clock advancement and isolated bodies. Network probes use
real owning RPCs and transport shaping, with labelled setup grants and teleports.
The normal-practice probe instead travels with native movement, completes two
ordinary courier jobs, buys and reads Spark with their rewards, then uses the
panel's preview/arming/held-commit functions. It changes no resource balances,
world clock or body positions directly. Headless setup closes the lobby input
mode explicitly, matching the rendered lobby transition.

The first cooked ordinary-run attempt had no AI navigation path. The autopilot
now falls back to a geometrically walkable, swept clear segment and still uses
native movement/collision; it never teleports. This verifies player travel, not
cooked NPC navigation. Revalidate authored cooked navigation in B45. No map asset
or runtime navigation behavior was changed to make this diagnostic pass.

These are automated engineering cases. They do not establish human usability,
balanced build diversity, a normal 25-minute match, a separate 90-minute soak,
four-human capacity or earned Seal acquisition. Existing courier geography and
repeatable rewards remain preview behavior until B45's connected region, visits
and eight-paid-job budget. The existing 64-entity view bound remains unchanged.

## Executed evidence

All paths below are relative to repository-root `Artifacts/`. Engine: Unreal
5.8.3 CL 58210709; compiler: MSVC 14.44.35229, Windows SDK 10.0.26100.0. These are
automated engineering results. Failed intermediate runs remain in ignored
Artifacts and are excluded from passing evidence.

| Check | Result | Evidence |
| --- | --- | --- |
| Final Editor and Game build/cook/stage/archive | Passed, Win64 Development | `20261020-101519-724-PackageWin64/Package.log` |
| Saved catalog/assets | Passed, 24 packages; no regeneration | `20261020-101504-047-Bootstrap-Validate/Bootstrap.log` |
| Native rule/schema suite | Passed, 13 tests | `20261020-100341-622-Automation/Report/index.json` |
| Authority preparation/books | Passed, 61 checks | `20261020-100414-422-Preparation-DevelopmentCheck/Preparation.json` |
| Ordinary courier-funded Rogue/Spark/panel demonstration | Passed, 14 checks; 5,768.02 cm native travel | `20261020-095956-322-Preparation-Normal/Normal.json` |
| Two remote clients, configured 100 ms RTT / 25 ms jitter | Passed, Server 9 / A 6 / B 6 | `20261020-100428-336-Preparation-DevelopmentNetwork-100-25/` |
| Two remote clients, configured 150 ms RTT / 25 ms jitter | Passed, Server 9 / A 6 / B 6 | `20261020-100503-154-Preparation-DevelopmentNetwork-150-25/` |
| Native 720p / 1080p panel | Captured and visually reviewed; confirmation controls fit, library scrolls | `20261020-102009-199-Preparation-DevelopmentUI-720/Preparation.png`; `20261020-101944-266-Preparation-DevelopmentUI-1080/Preparation.png` |
| Retained B42 skills | Passed, 62 checks | `20261020-100636-881-Skills-DevelopmentCheck/Skills.json` |
| Retained B41 character state | Passed, 24 checks | `20261020-100650-344-DevelopmentCheck/Local.json` |
| Ten native world reloads, now including books/channels/queues | Passed, 53 checks | `20261020-100703-147-DevelopmentReset/Reset.json` |
| Legacy combat/movement/life | Passed, 48 checks | `20261020-100724-669-CombatCheck/Combat.json` |
| Legacy objective scenarios | Passed, 53 / 3 / 3 / 4 / 5 checks | `20261020-100738-152-ObjectiveCheck/` |
| Cooked preparation/books | Passed, 61 checks | `20261020-101850-127-Packaged-Preparation-DevelopmentCheck/Preparation.json` |
| Cooked ordinary demonstration | Passed, 14 checks; 5,624.23 cm native travel using clear-segment steering | `20261020-101749-020-Packaged-Preparation-Normal/Normal.json` |
| Cooked two-client transactions, configured 150 ms / 25 ms jitter | Passed, Server 9 / A 6 / B 6 | `20261020-101852-721-Packaged-Preparation-DevelopmentNetwork-150-25/` |
| Cooked B42 skills / B41 state | Passed, 62 / 24 checks | `20261020-101917-659-Packaged-Skills-DevelopmentCheck/Skills.json`; `20261020-101920-260-Packaged-DevelopmentCheck/Local.json` |
| Ten cooked world reloads | Passed, 53 checks | `20261020-101922-854-Packaged-DevelopmentReset/Reset.json` |
| Cooked 720p panel | Captured and visually reviewed | `20261020-101933-560-Packaged-Preparation-DevelopmentUI-720/Preparation.png` |
| Fresh local checkout of `f5da328` | Passed: preflight, 24 LFS packages, cold Editor/Game builds, saved-asset validation and 13 rules; checkout stayed clean | `20261020-102145-187-CleanCheckout/Result.json` |

Network cases use real owner RPCs for duplicate purchases, held reading,
interruption, complete preparation and stale/replayed plans. The foreign-book ID
attempt is explicitly an adversarial authority fixture; it is not information
sent to another client. Restricted clients receive only their own book/build
state and have no canonical book or opponent body carrier. Native 100 ms
configuration measured median application echoes of 133.06 / 133.28 ms from
80 / 82 samples; native 150 ms measured 183.04 / 183.09 ms from 80 / 81 samples.
Cooked 150 ms measured 204.90 / 205.62 ms from 81 / 80 samples. These include
frame scheduling and do not establish a 250 ms p95 action acknowledgement.

The ordinary runs start with no resources, complete two original courier jobs
for 50 gold / 90 XP, spend 40 gold on Spark, read it without SP or auto-equipment,
then spend two attribute points and one SP through the panel's held station
transaction. Maximum HP rises from 95 to 100 while current HP stays 95.
The two contrasting level-four Rogue builds use labelled progression fixtures;
they are engineering examples, not human build-diversity evidence.

Runnable archive: `20261020-101519-724-PackageWin64/Archive/Windows`.
Open `Lockdown.exe`, Host, choose a class, Ready and Start; P opens the panel.
The package manifest records its actual pre-commit source revision `8eda790`
with `sourceDirty=true`, 82 source/config hashes and seven payload hashes.
`Artifacts/B43DocumentationAndProvenance.json` verifies these hashes, 67 Markdown
files/local links, 39 feature rows, 48 acceptance rows and four unchanged frozen
snapshots. No saved package or frozen specification was replaced.

Fresh-checkout verification copied no project binaries or Intermediate directory.
It reused the installed engine/shared caches and hydrated LFS from the local
source object cache. It verifies local reproducibility, not remote Git/LFS delivery.

## Next milestone

B44 supplies production Charter capabilities, stable humanoid wallets, one-draw
theft receipts, pair retry windows, Alert and exposure. Reuse B42's shared skill
Validate/Accept/Commit boundary and retained contender state. B45 supplies actual
region/NPC/reward content; B46 supplies earned Seal fabrication. Preserve the
role-neutral eight-field observation contract and normal-profile fixture ban.
