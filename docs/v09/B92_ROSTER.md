# B92 — persistent roster and native admission

29 October 2026 · B92 remains **in progress**

Native source **60e4dafd84d5d58ac06b22587a15e274424d06a4**. [Frozen evidence](evidence/B92Roster/README.md)
records the actual inputs, native runs and retained regressions. This increment
advances B92-T01 and the identity/death portion of T11. It does not complete the
living population, normal economy access or the whole stage.

## Review profile and delivered behavior

`v09_population`, selected with `LDB92GameMode`, is an explicit full-map population
review. `v09_full` admission remains closed. The existing environment and legacy
profiles retain their original game modes and behavior. Start the review with:

```text
/Game/Lockdown/Generated/V09/L_V09Full?game=/Script/Lockdown.LDB92GameMode?Profile=v09_population?Humans=4
```

`Humans` defaults to four and accepts integer capacities one through twelve.
The limit bounds human admissions, not total contender identities. With no
connections, all 24 seats have authority-only seat controllers. Four connections
replace four, leaving 20; twelve leave 12. There are always 24 contender seats,
not 24 plus the human count. The engine session allowance accommodates the
authority controller objects; B92 checks the configured human limit before
creating a human's contender state or granting anything.

`ALDB92Roster` creates one match domain, 24 persistent contender IDs and 196
disjoint noncontender IDs. It imports the selected 152 civilian/traveler/guard
assignments and adds 20 shared official positions and 24 hostile role capacities.
Every source gets an actual native Character body. The roster fits supported,
capsule-clear, locally navigable pads within ten metres, without reusing an
occupied initial pad. Civilian homes use exterior front approaches rather than
sealed interiors; existing service/building/settlement IDs stay authoritative.

| Native body role | Count | Current behavior boundary |
| --- | ---: | --- |
| Civilian | 96 | Persistent identity, authored home/work/rest references, three-gold account; routine/flee behavior pending |
| Traveler | 32 | Persistent identity and validated distinct origin/destination; route execution/sealed visual cargo pending |
| Guard | 24 | Persistent home identity and body capacity; guard stats, patrol, incidents, combat and 90-second recovery pending |
| Official | 20 | Nonreplicated body with damage protection; shared service execution/UI pending |
| Hostile | 24 | 12 camp locations, one boss location and 11 ambient role positions; encounter statistics/behavior/finite source rewards pending |
| Contender | 24 | Real private build, inventory, controller, body, life/return and wallet/bank identity; autonomous policy pending |

The 196 role bodies deliberately do not use legacy `ALDResident`. This avoids
random wandering, its 20-second generic respawn and lazy 36-gold purse grants.
Role behavior and appropriate combat statistics still need implementation. A
guard/boss-shaped capacity body is not acceptance of a working guard or boss.

The roster feeds its actual 24 contender and 96 civilian IDs into the
[five-pool economy authority](B92_ECONOMY_FOUNDATION.md). Initial gold remains
3,288; all contender wallets/banks and inventories start empty. The economy's
clock now follows native authoritative elapsed match time in this review. The
roster, identity/control metadata, institutional ledger and role bodies are
nonreplicated. AI seat controllers/player states also remain authority-only.
Human canonical bodies retain owner-only relevance; other observers use the
existing restricted B91 view. No human/AI or role flag was added to that view.

## Control changes preserve the contender

Human admission possesses an existing seat's body. It does not create a fresh
character, change life, reinitialize a build/inventory, choose a new spawn,
restore HP/MP or refill rewards. Initial seat presets use the nine inherited
starters. Selecting a fresh preset on takeover is not implemented or assumed.

Disconnect/controller destruction transfers the same record/body back to an
authority seat controller **before** native PlayerController teardown could
destroy its pawn. Logout and PawnLeavingGame share that idempotent release.
The previous controller loses its authority record, so it cannot authorize
old requests. A private reliable admission receipt seeds the new controller
above the previous input/body cancellation watermarks; its first action cannot
be rejected merely because the same body has already processed larger tokens.
Owner state arrives before cargo presentation without resetting body watermarks.
A repeated release cannot debit or grant again. Inventory actors
change their physical holder to the new controller while retaining item IDs,
bindings, revisions, fuel and creation lineage.

The handover first settles accepted past combat events and cancels pending
orders/channels/preparation/trade. Carried heavy property keeps its existing
carried-world binding; changing control does not require a new placement pad.
This interrupts input/lifecycle activity while retaining paid costs and cooldowns.
The body and its combat identity remain in place. Traversal interruption,
committed projectile/aura ownership and adversarial network timing still need
integrated handover evidence.

A seat waiting to return can change controller without receiving an early body
or a new return deadline. Ordinary contender death drops the original portable
tools and charges the inherited ceiling 15% wallet loss, retaining bank savings.
That loss now has one receipt in the full-profile wallet ledger. The inherited
peer-transfer hook also projects conserved wallet exchanges to that ledger;
item-only exchanges do not require an unrelated currency sink. The review
rejects unclassified reward/financial mutations rather than silently creating
money while other economy services remain disconnected. Ordinary timed return
creates one new body life under the same contender ID, accounts and budgets.

Admission currently chooses the first available seat. It is not an authenticated
account-reclaim/rejoin policy: another connection can occupy an available seat.
Account authentication, reconnect ownership and full saved-match recovery remain
separate integration work.

## Native evidence

| Run | Result |
| --- | --- |
| Four-human-capacity fixture | **37 checks**; four real authority controller admissions, 20 AI seat hosts, 196 role bodies, 220 total |
| Twelve-human-capacity fixture | **53 checks**; twelve authority controller admissions, 12 AI seat hosts, 196 role bodies, 220 total |
| Both lifecycle cases | Same body/life on controller destruction; retained XP/build, cooldown, wallet/bank, work counters and Witness; waiting-life takeover; actual-clock native return; exact original bound Torch and carried crate follow control then drop on death, retaining 42-second Torch fuel |
| Both economic closures | **Five receipts**, 149 accounts, 426 institutional item IDs, zero mint, three death-tax burn, 3,285 final gold; every finite contract/producer budget unchanged |
| Economy foundation regression | **125 checks**, independently replayed complete/poor/late schedule domains |
| Rule regression | **58 passed**, including private-view schemas and inherited/full catalog/starter checks |
| B91 source/pointer regression | **53 checks** across two real remote owners, retaining source controls, finite harvest/chop, ordinary weapon checks and life return |
| Builds | Editor and Game Development, full unity, pinned UE5.8.4 CL58902052 |
| Protected inputs | 148 protected assets and 59 supplied files byte-for-byte unchanged |

The four/twelve admissions are native controller fixtures on one dedicated
authority, **not four/twelve remote clients or human playtests**. The regression
with two remote owners uses the B91 environment; it does not establish B92
network takeover acceptance. No population-density/FPS/tier cost, routine
timeline, witnessed guard response or autonomous progression is claimed.

The lifecycle setup explicitly transfers 30 existing Capital reserve gold into
wallet20/bank10, round-trips one gold through the inherited peer-accounting
hook, awards diagnostic XP135, seeds existing proof/work counters and
prepares one original finite world Torch and one existing carried crate. Those are labelled fixtures, not legal
work/progression or acquisition. All subsequent control/destruction/death/drop/
return behavior runs through native authority paths. Death burns three from
wallet20 and retains wallet17/bank10; neither takeover nor return mints money.
The native record retains the same economic identity through the entire case.

The first compile caught three implementation mistakes: an Actor member-name
shadow, deduction of a raw pointer from Unreal's TObjectPtr, and an attempted
write through the inventory's const lookup. All were corrected before the final
native runs. The failed build log is retained separately as diagnostic evidence.

Reproduce strictly sequentially:

```powershell
python -B Scripts/SyncB92Population.py --check
python -B Scripts/SyncB92Economy.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 2 -FullUnity
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB92Roster.ps1 -Humans 12
./Scripts/RunTests.ps1
./Scripts/RunB92Economy.ps1
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/BuildGame.ps1 -MaxParallelActions 2 -FullUnity
python -B Scripts/CheckB92RosterEvidence.py docs/v09/evidence/B92Roster
```

## Next integration gates

Connect remote admission/takeover validation and five physical LMB service
endpoints to real inventory/access/capacity guards. Combine the B91 world,
container and installed-property census with institutional/owner accounting into
one full-match item ledger. The current review keeps those two disjoint scopes
explicit; it cannot admit a complete `v09_full` match yet.

Then connect globally shared rare stock, cross-city banking, finite deposits,
physical courier tokens, procurement/crafting, full-map prepared Witness and
the earned zero-kill registered Charter journey. Execute the imported routines,
local witnessed security and same-ID guard recovery; give ordinary contender
AI legal owned/observed/public inputs and commands. Validate wake tiers, live
effect/cargo handover, recovery and all remaining B92 exits before proceeding
to B93 victory integration. B94 owns full-session/human/performance acceptance.
