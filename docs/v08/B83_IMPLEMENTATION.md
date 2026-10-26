# B83 environmental fields

B83 extends the explicit v08 integration profile with six cell conditions and
finite Oil, all ten environmental transitions, paid projectile reservations,
contact damage and cell-based smoke/steam authorization. This remains an
engineering milestone; the complete catalog/acquisition, route demonstrations
and human/release acceptance remain B84–B86 work.

## Authority and lifecycle

The200cm grid retains origin(-6000,-4000). Commands select deterministic
rectangle/disk/cone masks capped16 cells for new actions and32 for Smoke.
Hidden bodies and private source records never influence geometric selection.
Static walls clip the same masks used by native preview and server mutation.
Unmatched substrate creates no state. Public soil, authored fuel sockets and
metal aprons are authored separately from arbitrary scenery trees.

Each state stores creator, original source life/cast, resource sponsor,
creation/expiry, magnitude and revision. Allocation is per distinct live
owner-cell, with64 per sponsor and256 per match including pending work.
Release validates a complete transition plan before costs; commit applies
that plan once. Expiry precedes allocation. Worst-case projectile cells and
transition credit stay held through impact, block or miss. Reflection changes
effect attribution and retains the original sponsor. A bounded presentation
pool is allocated before payment, so later actor allocation cannot discard a
paid field. Wards/traps/gates retain their separate lifetime and emitter limits.

Water/Frost/Fire/Lightning/Wind/Nature/Shatter/Smoke/Oil use nonrecursive
transitions. Frozen retains the temporary Wet substrate without making a
separate Wet record immortal. New command snapshots apply trait bonuses once
then clamp: Wet/Frozen10s, Burning6s, Electrified4s, Obscured6s, Growth/Oil8s.
Existing states do not add intensity or duration. Oil is consumed by ignition
and does not regenerate. Source death leaves committed environmental records
until their own expiry. No element changes collision, navigation or elevation.

Direct hostile Frost retains B82's600ms Stun attempt and contender-wide6s
readiness. Direct Fire consumes Wet or adds3s Burning; Water removes Burning.
Surface contact never triggers a Frost Stun. Contact Wet/expiry precede direct
packets, then surface commands and due500ms damage, death and Heart resolution.
Burning surface20 raw magical DPS replaces overlapping unit Burning12;
Electrified18 is a separate component. Ambient contact includes the creator,
respects protection, and uses ordinary mitigation/barriers and existing finite
reward ledgers. Damage attribution uses the creator's original life, including
after death, rather than the supplier of Wet or a later replacement body.

## Native content and observation

The enabled slice is35 learned abilities,61 items and nine presets. It adds
Firebolt39, Gust41, Entangle42, Chain Lightning43, Shatter Burst52 and Conductive
Field53, plus Oil Flask91 and Gust Vial93. Sparse schema8 ranks extend to54
identities while schema7 stays38. Firebolt pays for one projectile/impact mask;
its block/miss releases the reservation once. Chain Lightning selects at most
three authorized targets with deterministic jump ordering; each packet may
block separately and receives only its authored20 raw Wet bonus. Entangle's
direct Root is separate from Growth's15% contact slow. Shatter consumes selected
Frozen cells and hits each original victim life once without Wet-stunning.
Smoke Veil/Flask use the common Obscured mask. Existing v07 paths remain separate.

The active UFS payloads and compiled hashes come from `SyncB83Data.py`.
Original specification inputs, B80/B81/B82 payloads and evidence remain frozen.
The B82 checker was coupled to its native enum snapshot; use the B83 checker
for the current native reservations. Full catalog/recipe/modifier coverage
remains in B84; unsupported definitions stay hidden, including Ember Line until
its complete damage-zone lifecycle is migrated.

Smoke/Steam uses symmetric external boundaries of the actual cell union.
Internal shared cell boundaries are transparent; rays leaving/reentering the
union remain blocked. Static walls and ordinary sight limits still apply.
Detection does not cross Obscured boundaries, and an authorized ward may provide
its own inside view. Gust clears selected cells and recomputes authorization.
Visible front cloud cells may show their surface without revealing enclosed
units. Observations contain bounded appearance/pose/condition bits, never the
canonical grid, sponsor, creator, life or hidden build. Owner-only committed
geometric masks contain no hidden occupancy or creator data. Bounded tuple
capacity is512, using an explicit wide-count escape above254 entries.

## Validation

Win64 Development Editor build passes on UE5.8.4 CL58902052,
MSVC14.44.35229 / SDK10.0.26100.0. Two compiler workers were used under local
memory pressure, within the maximum of four. Unreal operations ran sequentially
with Source/Config frozen.

| Check | Final result | Artifact |
| --- | --- | --- |
| Editor build | PASS | Artifacts/20261026-134029-324-BuildEditor |
| Active v08 regeneration / four-map validation | PASS | Artifacts/20261026-133307-922-Bootstrap-Generate |
| Compiled native rules | 45 passed, 0 failed | Artifacts/20261026-134104-608-Automation |
| B83 server / independent clients A,B | 114 / 40 / 40 checks, 0 failures | Artifacts/20261026-134131-386-B83-Fields-100-10 |
| Retained B82 fixture | 56 / 23 / 23 checks | Artifacts/20261026-134301-607-B82-Rivers-100-10 |
| Retained B81 fixture | 28 / 17 / 16 checks | Artifacts/20261026-134410-693-B81-SlotsTraits-100-10 |
| Separate v07 loadout, isolated rerun | 46 / 27 / 26 checks | Artifacts/20261026-134743-795-B73-Loadout-100-10 |
| Independent ledger replay | 33 receipts / 9 accounts / 105 unique items reconcile | evidence/B83/LedgerReplay.txt |

The first v07 loadout run (Artifacts/20261026-134458-902-B73-Loadout-100-10)
had five server assertion failures following an uncommitted held preparation;
both clients passed. An isolated rerun with the same modules passed all checks.
The cause is not established. Both reports are preserved (`V07FirstRun*` and
`V07Regression*`); intermittent held-transaction validation remains open and
blocks any claim of complete release verification.

The network run uses an Editor server and two separate native clients with
100ms RTT and 10ms jitter. The paid journey purchases/reads/prepares Firebolt,
uses physical Oil/Smoke/Gust sources, releases the projectile and observes
smoke hiding and subsequent corridor clearing. It retains actual player/AI
river movement and the B82 held book, preparation, direct-Frost and claim checks.
Wallet funding, initial item creation, knowledge/equip/MP setup, phase placement,
health and isolated authority clock advancement are explicitly diagnostic.
They are not normal earned progression or human/rendered-input acceptance.

The isolated native service lane checks all ten transition effects, costed
Entangle/Conductive/Shatter/Chain/Gust, strongest Burning contact, cleanse versus
intact surface, original-life field persistence and native camp reward after
creator death, duplicate reward rejection, real projectile block/reflection,
64/256 allocation, exactly 512 evaluations then rejection, expiry and private
nonreplicated canonical actors. Compiled tests add invalid substrate, state
caps/sponsorship/revision, union ray geometry and wire rejection boundaries.
Each assertion's actual setup is preserved in the portable network reports.
The full independent-client adverse/source/ordering matrix remains partial.

On the recorded AMD Ryzen9 7945HX / 16GB Windows11 host, 200 synchronous
256-cell **maintenance-only** samples measured p95=0.121ms and p99=0.136ms.
This lane has two remote contenders, native bodies and four budget-owner IDs.
It excludes full damage/vision publication, packet volume and frame-rate costs;
it does not establish the four-contender service performance acceptance target.
The bounded synthetic 512-tuple view serializes to 17,794 bytes; the retained
128-tuple view still fits 8KiB. This is serialization size evidence, not measured
bandwidth or target-platform frame rate.

Portable reports, final build log, region validation, static checks and normalized
source/asset hashes live in [evidence/B83](evidence/B83/Summary.json). Active B83
checking, historical B81 checking, nine B80 importer rejection tests and all four
v07 sync checks pass. Original specification checking retains 1812 assertions
and 18 reference tests; those are specification-model evidence.

The working ledger has 366 rows. V08-028–045, ten reactions and affected enabled
catalog/operation rows are marked partial with focused native evidence. The
original specification acceptance templates and previous checkpoints remain
unchanged.

## Remaining scope

B84 owns complete catalog/acquisition/modifier/proc coverage and the complete
Ember Line lifecycle; B85 owns all four normal no-grant world journeys and full
source/sink audits; B86 owns native usability, human feedback and release checks.
Full independent-client adverse races, all self/prior-life/protection/hidden-chain
combinations, four-contender performance including replication/frame rate,
physical/rendered input, fresh cold packaging/distribution and long sessions
remain pending/deferred as named. Focused engineering results do not waive
E01–E48 or V07-D01–D08/V06-D01–D08 inherited acceptance and deferrals.
