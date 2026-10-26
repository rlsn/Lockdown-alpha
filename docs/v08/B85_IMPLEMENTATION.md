# B85 world integration

B85 carries the complete B84 catalog through the existing world, finite sources,
economy and Heart rules in the explicit `v08_integration` profile. It adds native
v08 route demonstrations and a camp decision policy. B86 remains the native
usability and release checkpoint; historical specifications and evidence stay
unchanged.

## Implementation

The retained world probe now selects the current profile's acquisition checksum,
starter inventory and physical weapon binding. A paid two-handed Surveyor Bow
uses main hand5/offhand6 in v08, through the ordinary held preparation service;
observed attacks carry schema8/wire4, basic source kind and the current inventory
revision through the normal server request boundary. V07 keeps its previous
binding and request format. The route runners accept an explicit profile and
select L_V08Region or L_V07Region accordingly. Their default remains v07.

Each normal journey uses two local native contenders, real navigation and
movement, fixed20Hz benchmark time, ordinary protection/quiet periods, held
services, finite caches and encounter AI. No teleports, resource/knowledge/HP
grants, progression setters, forced deaths, access setters or clock jumps occur
in the route lane. Automated commands are used; these are not human or packaged
distribution demonstrations.

* Keys visits all three ruins, opens each finite cache, takes its unique physical
  key, activates the retained personal breach and completes a physical Heart
  claim. No gold is minted or spent. A second contender travels from City in an
  unchanged level-one Arcanist start, observes the claimant and releases its
  starter Counterpulse. The actual explicit interrupt cancels the first claim,
  spends32MP and commits cooldown. The key lease survives and a fresh ten-second
  claim wins. The defender has no XP, wallet balance or equipment.
* Siege earns eight courier payments, buys and quietly equips a Surveyor Bow,
  fights a finite camp and all three towers with native observed attacks and
  movement, recovers at shrines and claims through permanent global access.
* Artifact earns its money by courier work, buys inputs from official supply
  and the regional market,
  crafts the two physical Cores, retrieves a finite Catalyst, defeats the real
  Guardian, picks up its physical Fragment and assembles Heartbreaker at
  HeartForge. Activation consumes that instance before the personal lease and
  final claim.
* Charter earns its money and natural allocation, crafts the two endorsements,
  completes equipped Heave for a Witness, pays100 gold at Treasury and registers
  the public window. The unregistered second contender wins through that same
  public access after travelling to the Heart.

V08 finite camp bodies have a declared valid level-one Arcanist spell kit and
initial MP, without a wallet or inventory grant. Their decision reads a currently
visible target's public Wet cue and rejects occluded lines. They request
Frostline through the same native slot/gate/MP/Silence/cooldown boundary, stop
while winding up and otherwise retain their normal attack/chase/leash behavior.
Boss/tower stats, category immunities, finite rewards and source identities are
unchanged. World bodies retain authored HP and leash-only health recovery;
generic humanoid spell-build regeneration must not turn a260HP camp into600HP.
Residents keep their peaceful routines and service bodies remain noncontending.

The separate labelled boundary lane tests the actual camp policy, including
paid release, victim-wide Frost reaction, cooldown and Silence. It also extends
the physical key/death fixture through real return: next life, full current
HP/MP, three-second protection, unchanged dropped identity/reuse endpoint and no
resurrected lease. Boundary placement, status and physical source setup are
diagnostic and are never counted as normal route evidence.

An existing build issue also surfaced when adaptive unity regrouped the B82,
B83 and B84 probes: their anonymous helper names collided. Each probe now has
distinct helper names, preserving behavior and allowing those files to compile
together.

## Validation and accounting

The portable evidence bundle is `evidence/B85`. Its Summary records exact native
commands, artifact directories, assertion counts and scope. Every route and
boundary ledger is independently replayed by CheckEconomyEvidence.py, including
account revisions, gold conservation, finite budgets, physical identities,
consumption and production parents. CheckB85Evidence.py additionally verifies
all four schema8 reports, absence of diagnostic receipts/starter inventory,
route-specific receipts and the level-one interruption.

Validation passes386 route assertions,287 assertions across20 separate boundary
cases,47 native rule tests and all28 independent ledger replays. Full-unity
compilation and all four regional topologies pass. At100ms round-trip latency
with10ms jitter, B84 passes536/40/40 server/client assertions, B83 passes114/40/40,
B81 passes28/17/16 and retained v07 loadout passes46/27/26. These network lanes
retain explicitly diagnostic setup; they are separate from the earned routes.

| Route | Native world seconds | Checks | Receipts | Minted | Burned | Final total gold |
|---|---:|---:|---:|---:|---:|---:|
| Keys | 163.40 | 35 | 9 | 0 | 0 | 450 |
| Siege | 816.85 | 85 | 36 | 332 | 70 | 712 |
| Artifact | 746.30 | 141 | 55 | 290 | 174 | 566 |
| Charter | 528.85 | 125 | 42 | 330 | 188 | 592 |

The450 starting gold belongs to declared market/institution accounts; both
contenders begin at zero. Totals include all accounts, not just the winner's
wallet. Charter's330 minted gold includes200 from eight couriers and130 from
the two normal procurement deliveries. Physical identity counts are93/105/123/114
respectively, with all production parents and consumption reconciled.

Keys/Siege/Artifact reports come from the full-unity build's four-route run.
Charter was rerun after correcting only its stat-preparation draft envelope to
carry current inventory revision and eleven bindings; that branch is not used
by the other routes. The SelfAction boundary fixture was likewise updated to
send matching v08 catalog and combat envelopes; that affected case was rerun,
together with the remaining activation/AI cases, before final rules/network
regressions. Summary.json records the exact build/run directories. Earlier failed
harness attempts remain in local Artifacts and are not counted as passes.

Reproduction:

```powershell
python -B Scripts/SyncB84Data.py --check
python -B Scripts/TestV08Import.py
python -B docs/v08/tools/check_spec.py
./Scripts/BuildEditor.ps1 -MaxParallelActions 2
./Scripts/RunTests.ps1
./Scripts/RunWorldCheck.ps1 -Profile v08_integration
./Scripts/RunWorldBoundaryCheck.ps1 -Profile v08_integration
./Scripts/RunB84Check.ps1 -RoundTripMs 100 -JitterMs 10
```

Source and Config remain frozen during each native operation. No map geometry,
navigation, assets, frozen specification data or catalog payloads change in B85.

## Remaining acceptance

These are scoped engineering demonstrations with two local contenders and
separate diagnostic/network fixtures. They do not close every per-definition
independent-client/adverse combination, rendered/native input, human feedback,
cold packaging, long sessions or four-contender full-service/replication/frame
performance. Carry forward original-life/protection/hidden-chain combinations,
proc transfer/death timing and the historical intermittent v07 held-preparation
failure; a passing rerun does not establish its root cause. E01–E48,
V07-D01–D08 and V06-D01–D08 remain preserved and mapped. Default frontend/V05 and
explicit v07 remain separate. No old-executable compatibility, live-match
migration or new reconnect mechanism is claimed.
