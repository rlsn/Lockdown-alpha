# B82 river core

B82 adds the native Wet/Frozen slice to `v08_integration`. B83 remains the
complete environmental service milestone; this is not full v0.8 release
acceptance. B80/B81 payloads and evidence, the original specification and the
separate v07 region remain historical inputs.

## Implemented behavior

- `L_V08Region` copies the retained world layout into a separate native asset,
  with blue static river floors at the near-city ford and the Heart approach.
  Both connect to ordinary banks and have adjacent dry routes. Elements never
  change floor collision, navigation links, elevation or rebuild navigation.
- Server movement contact applies Wet on entry, then the authority clock
  refreshes it every100ms. Exit leaves3000ms residual, or1500ms with bound River
  Waders. Frozen river keeps its underlying water. Frozen contact slows30%,
  or15% with bound River Treads, and never stuns. Exit/expiry removes that slow.
  Unit cleanse removes Wet; continued contact refreshes it at the next boundary.
- Frostline and Water Jet have their authored line geometry, rank magnitudes,
  windups, actual MP, cooldowns, natural equip gates and paid City books.
  Arcanist now starts with its authored Frostline/Counterpulse teaching kit.
  Frost Prism, Water Flask, Frost Capsule, Waders and Treads join enabled supply.
  Their physical references, revisions, consumption and cooldowns use B81's
  shared native loadout/transaction paths.
- Only direct hostile Frost can attempt the Wet reaction. Packet block and
  original-life checks run first. An eligible Wet victim consumes Wet and sets
  its contender-wide readiness to exactly now+6000ms, then attempts a fixed
  600ms Stun through existing category immunity and Control Guard. Locked hits
  leave Wet/readiness unchanged; duplicates cannot repeat an attempt. Death,
  return and strong dispel do not reset the contender endpoint. No new reconnect
  mechanism is claimed.
- Due applied hard control reaches the existing Heart claim cancellation path
  before same-boundary completion. Suppressed Stun, Wet, contact slow and
  nonlethal damage alone do not cancel a surviving stationary claim.
- The bounded two-state service uses200cm cells aligned to(-6000,-4000), max16
  selected cells per action,64 distinct owner cells and256 match cells. Costs
  follow allocation checks. Wet and Frozen records retain original source life,
  owner/cast and expiry; committed surfaces survive caster death. Field Practice
  adds1000ms to newly created Frozen once, capped at10s. Static river substrate
  consumes no dynamic allocation.
- Owner input/tooltips/HUD include the new actions, bounded grid footprint,
  Wet feedback and private shared readiness. World cell cues go through existing
  sight-filtered observations; clients receive visible appearance/pose, never
  the canonical grid or hidden owner/source-life records.

The active catalog is29 learned abilities and59 items, with all nine presets.
Frostline retains reserved identity38 and Water Jet40; Firebolt39 stays disabled.
Schema8 ranks extend to41 identities, including the reserved hole, alongside
the unchanged38-identity schema7 contract. Active payload hashes are compiled
from `combat_b82.json` and `equipment_b82.json` by `Scripts/SyncB82Data.py`.
Binary interoperability with older executables is not claimed.

## Validation

Final Win64 Development Editor build passes on UE5.8.4 CL58902052,
MSVC14.44.35229 / SDK10.0.26100.0. One compiler worker was used under local
memory pressure, within the existing maximum of four. Native operations ran
sequentially with Source/Config frozen.

| Check | Final result | Local artifact |
| --- | --- | --- |
| Editor build | PASS | Artifacts/20261026-104424-116-BuildEditor |
| Region generation/validation | Four maps valid; three new assets only | Artifacts/20261026-102346-606-Bootstrap-Generate |
| Compiled native rules | 43 passed,0 failed | Artifacts/20261026-104850-593-Automation |
| B82 server / independent clients A,B | 56 /23 /23 checks passed | Artifacts/20261026-104601-853-B82-Rivers-100-10 |
| Retained B81 loadout fixture | 28 /17 /16 checks passed | Artifacts/20261026-105118-540-B81-SlotsTraits-100-10 |
| Separate v07 loadout fixture | 46 /27 /26 checks passed | Artifacts/20261026-105305-066-B73-Loadout-100-10 |
| Independent ledger replay | 22 receipts,9 accounts,101 unique items reconcile | evidence/B82/LedgerReplay.txt |

The network runs use an Editor server and two separate native clients with
100ms RTT and10ms jitter. These are not packaged-server or rendered-input
acceptance. The B82 fixture uses actual player movement and native AI
pathfollowing across the city ford before, during and after Frozen. It also
checks complete native Heart ford/dry-detour paths and contact at Heart water.
Paid Water Jet acquisition burns40g, held reading consumes that physical book,
and quiet preparation equips the resulting rank. Other item grants, initial
wallet funding, phase placement/health and isolated boundary clock setup are
explicitly diagnostic. They do not establish normal earned progression.

Native receipts cover capsule Wet/contact slow, cleanse refresh, exact3000/
1500ms residuals, Treads without stun immunity, Frostline/Water Jet MP and
cooldowns, reusable Prism and consumable Flask/Capsule sources, alternate AI
attacker sharing the same victim lock, source-death field persistence,
victim-death/return readiness, stale original life,64-cell owner rejection before
cost and expiry. Actual due Prism packets test block preserving Wet/guard/lock,
Stun cancelling at Heart completion and a guarded attempt allowing completion.
Compiled rules additionally exercise dry Frost,6999/7000ms readiness, immutable
reservations, extended build/wire round trips and bounded observation tuples.

Portable reports, region validation, final build log, data checks and normalized
source/asset hashes are in [evidence/B82](evidence/B82/Summary.json). All four
v07 sync checks pass unchanged. Historical B81 payload checking and nine B80
import rejection tests pass; the latter now isolates retained enum prefixes in
temporary fixtures, while the active B82 checker validates current explicit
native reservations. Original spec checking still passes1812 assertions and18
reference tests; those remain specification-model evidence.

The working ledger has365 rows. V08-016–027 and affected definitions are marked
partial with their actual focused evidence. Dry-target readiness boundaries
have compiled native rule evidence; the entire independent-client adverse
matrix is not claimed. Existing B80/B81 evidence remains historical.

## Remaining scope

B83 owns all six conditions/Oil, ten reactions, delayed projectile reservations,
damaging contact/tick attribution, complete overwrite/overload/visibility
integration and broader adverse races. Current previews use public river and
authorized cell cues; full mask/reservation confirmation under blind/fog and
overload races remains in that stage. Ember Line remains hidden in schema8
until its new Burning surface exists; v07 retains its original damage zone.
B84/B85 own the remaining catalog/acquisition and full world integration. B86
owns usability/release evidence. Human participation, physical/rendered input
matrices, cold packaged distribution, long runs and inherited named deferrals
remain pending. Automated native probes do not waive those requirements.
