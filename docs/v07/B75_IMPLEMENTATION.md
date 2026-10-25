# B75 — complete tactical content

25 October 2026. Based on B74 `25b84cd`. The explicit `v07_integration`
profile implements all28 tactical spells and all40 item definitions. B76's
authored world, guardian/towers and four-route outcomes, and B77's release
acceptance remain open. The normal frontend/V05 and `v06_regression` retain
their existing rules.

## Catalogs and starting choices

The checked combat import now contains all28 spells and the six adopted53-point
starter kits. Vanguard, Infiltrator, Arcanist, Seeker, Broker and Trickster each
start with their specified two Rank1 skills and no items or wallet grant. The
three B70-normalized economic starts remain available. Preset identity is
owner-only and distinct from the retained legacy class/subclass adapter. All
ordinary book knowledge remains cross-class; natural equip/cast gates and the
one-capstone limit still apply. Equipment imports all40 definitions, including
consumables, with typed active operations and full checked passive data.

Runtime reads generated native assets, never `docs/`. Sync scripts, checksum
contracts and `DA_CombatCatalog` / `DA_EquipmentCatalog` change together.
Acquisition metadata and its native asset retain the B74 source contract.
The Guild quote and native panel share all16 Circle-I books plus the four
retained legacy offers at40g. New presets suppress the legacy ward endowment;
the three retained economic starts keep that earlier entitlement.

## Release, defense and source lifecycle

The same server action queue owns targeting, current life/build/inventory
revisions, reservations, statuses, costs and final releases. Wire3 extends the
bounded tactical request with two observed handles/two points and exact owned
item revisions. Wire2 single-target requests remain readable. No client actor
reference, private enemy sheet or unbounded target array is introduced.

MP and ordinary item charges commit at release, after final validation and
fallible field/projectile allocation. Windup interruption spends none and adds
400ms recovery. Mana Draught and Recall Token instead consume once at the start
of their explicit held channels; release, stale hold, movement, positive damage,
relevant control, changed build or life cancels without restoring their committed
costs. They complete after due combat damage in the sample.

New spells provide Hamstring's strongest40% slow, Null Seal mute, directional
Brace Wall, Ledger Mark clues, a one-HP False Trail decoy, six-component Ember
Line, terrain/body Grapple, native Slipstream, destructible Arcane Prison,
Temporal Lock's visible windup, and Sovereign Ward's projectile boundary.
Earlier B71/B72 effects continue through their existing authority paths.

Ember Line captures its rank/INT damage at release and divides integer cents
over six500ms components, with the remainder in the final component. Targets
must occupy the rectangle and have geometric line of effect at that tick.
Prison creates24 native walking-collision segments, omits segments that would
overlap a body, and has no sight-wall response. Destroying its anchor removes
the collision. Decoys follow a static navigation path toward the selected
ground point, grant no vision/rewards/interactions, and use the source palette
through ordinary anonymous body observations. A detector adds an illusion cue.

Reflective Mail reverses one incoming projectile spell with captured original
coefficients. Reflected packets cannot reflect again; ordinary victim defenses
still apply on return. Dome boundary interception runs before reflection and
spell block. Basics, instant spells and walking inside do not become projectile
spells. Bow basics launch1200cm/s non-homing snapshot projectiles. An observed
A-confirm maintains only that original-life target, rechecking sight/range;
Stop, another action, target loss or life change cancels the order.

Null Dagger arms for4s and consumes only on a positive legal basic contact.
Its1.3s silence can be blocked separately from ordinary attack damage. Watchblade
marks every third contact, Oath Mace damages the remaining barrier, and Break
suppresses explicitly tagged passives without removing armor, attributes or
capacity. Healing Salve ticks20HP eight times and ends on positive contact,
including absorbed barrier damage. Item healing/mana/control durations do not
receive craftsmanship or INT scaling. Eligible item barriers alone use the
existing1.07/1.14 craftsmanship multiplier and floor rule.

## Marker, exchange and gate semantics

Guard Swap's placement costs10MP, lasts20s and imposes1s recovery without the
full cooldown. Execution costs40MP, validates the owned observable endpoint,
moves the caster, removes the marker and starts the rank cooldown. Replacement
and expiry refund nothing; enemy destruction starts full cooldown.

Grand Exchange validates both targets, range, pair separation, root/grounding,
ordinary-body/owned-construct category, walkable surfaces and capsule fits before
moving either. The first hostile blocking charge in selected-target order
cancels the whole packet and leaves other charges. Both actual movements
interrupt existing claims. Failed endpoint staging moves neither target.

Gate Weave requires explored endpoints, first within650cm plus cast bonus and
pair separation at most4000cm. Both emitters allocate before cost and replace
the previous pair together. Each has its own rank HP; either destruction,
expiry or owner-life loss closes both. Gates provide no remote vision. Ordinary
contenders inspect an observed endpoint within80cm and hold Context1s. Hard
control/root/grounding, positive damage, an active Heart claim, objective cargo,
stale hold or an occupied/invalid destination prevents travel. Arrival is
vulnerable and starts a persistent3s gate-use lock.

## Items, cooldowns and physical accounting

Every equipment active needs both an equipment binding and one of three quick
references. Bag consumables need a current quick reference. The bag Use button
directs active items to those controls; it cannot bypass binding/held input.
Active items use no MP, respect mute, and revalidate the exact physical revision.
Reusable items remain carried; consumables disappear once. Ward/trap charges
transfer their ledger identity into an active emitter and retire on trigger,
expiry, replacement or owner death, rather than being counted as both source
and consumed item. Skill/item ward and trap limits are separate.

Full cooldown families persist on the contender after consumption and merge
the maximum transferred instance endpoint. Blink sources share blink; grapple
sources share grapple; Null Bell/Null Staff share cleanse. Major protections
share the explicit8s activation lock; their longer reuse endpoints use their
individual definition keys. Assembly, quality, trading, death and rebinding
retain instance endpoints. Positive direct hostile HP or barrier contact adds
the persistent3s Blink/Rift lock; periodic, self and environmental damage do not.
Tower Relic is fixed Standard and always drops, even bound. Ordinary bound
equipment retains; spare gear/consumables/cargo drop through native inventory.

## Native cues and adapter decisions

Observed fields have public names and native shapes for markers, line zones,
prison rings, telegraphs, domes, gates and authored scramble points. Their
actors remain server-only; clients render sanitized proxies. Ledger Mark emits
only when current geometry authorizes the marked position. Its4s stale clues
can remain after sight is lost and never update through fog. Footstep cues are
anonymous and use a420cm geometric audible radius, reduced by the strongest
tagged20% reduction. Public observations remain8 reflected fields and public
GameState34. Owner payloads add only their own family/status/traversal data.

The retained native Plateau supplies one two-way authored Cliffwalkers link,
derived from its public geometry. A closed navigation area prevents ordinary
pathfinding from using it; equipped contenders use a fresh1s held traversal
with root/grounding and capsule validation. This is a B75 adapter on retained
geometry, not B76's complete world. Iron Oath's underspecified directional10%
passive is implemented as ten percentage points of front120-degree physical/
magic mitigation, folded into the respective60% cap and suppressed by Break.
These native adapter choices introduce no random dodge/stealth rule.

## Verification and remaining acceptance

| Native gate | Result |
| --- | --- |
| Editor build / native asset validation | Passed; three checked catalogs,28 retained packages |
| Rule automation | 39 passed |
| All-content authority | 403 checks; all28 spell and40 item outcome rows |
| B75 dedicated server / clients A,B | 21 /23 /19 passed;100ms RTT target,10ms jitter |
| Earned B74 acquisition journey | 63 checks passed |
| B74 / B73 / B72 / B71 network regressions | 17/29/25;46/27/26;59/35/34;34/18/15 |
| V05 authority regression | 56 passed |
| Independent ledger replay | Eight journals passed |

Verification results are recorded in `evidence/B75/Summary.json` with raw native
logs, rule report, checked imports, source manifest and independent ledger
replays. The effect lane explicitly grants diagnostic items/attributes/knowledge
and controls clock/placement. The network lane uses a dedicated server and two
independent clients through real wire3 RPCs. B74's separate earned acquisition
lane grants no money/items/XP and is retained as the normal transaction check.
The effect lane also starts two actual new presets with empty inventories and
zero wallets, earns50g through two native courier jobs, pays40g for Ember Line
and reads that physical book through the actual quiet3s transaction. Its later
combat setup is diagnostic; this source check is not a full normal route. None
of these fixtures certifies human travel, pacing or a complete route.

The fixed B74 caches/catalysts and paid/recipe sources now feed implemented
effects. Actual guardian/Fragment, tower/relic reward sources, complete authored
POIs, leases/siege/four-route terminal ordering and normal route demonstrations
remain B76. Full100/150ms/privacy permutations, rendered/physical input and OS
scale review, cold package, normal1/4-player35-minute sessions, restart/performance
and human acceptance remain B77 and inherited V06-D01–D08. No new package or
complete v0.7 release is claimed. Adopted specification bytes remain preserved;
the pre-existing user edit to `09_VICTORY_AND_MATCH_STATE.md` is excluded.
