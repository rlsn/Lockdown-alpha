# B76 — native world encounters and four-route outcome

25 October 2026. Base: `4ea55bc` (B75). The implementation and focused native
exit gates are complete. Full v0.7 delivery/human acceptance remains B77. The
adopted specification stays unchanged.

## Runtime

Explicit `v07_integration` standalone launches `L_V07Region`, an extension of the
existing native City/West/East region. The retained development and alternate
maps continue to support prior diagnostic lanes. The default frontend remains
V05. No engine/ability-system rewrite or replacement game was introduced.

`DA_WorldCatalog` imports the adopted world/reward data plus authored positions.
`SyncV07WorldData.py --check` reproduces the staged JSON, closed checksum header
and regional layout. The native region includes ruins, guardian lair, HeartForge,
shrines, precinct cover and three independently removable tower obstructions.
Native navmesh and collision govern ordinary movement. Camp B is authored at
(4800,2500), separated from the guardian, forge and East service. Camp C is
at (4800,-2800), outside the covered courier road. Quiet economic routes do not
implicitly require clearing an overlapping encounter. Source stats and
reward counts are unchanged.

Three camp pools each preallocate twelve match identities. Only one unit per
camp can live; a later spawn needs a dead predecessor and at least150s since
its spawn. Guardian and tower stats follow the adopted data. The guardian
chases, melees and telegraphs a400cm magical pulse for1.5s; leash healing does
not reset source budgets. Boss/tower immunity is limited to stun, hex, break, silence, mute and
displacement; root/disarm/slow/blind retain their ordinary effects. Towers choose
the nearest observed eligible player
with a stable life-ID tie, then fire a snapshot projectile. Each permanent
loss removes that tower's public sight and obstruction. All three down grants
global siege access.

Positive damage records the persistent last damaging contender. Death spends
one finite source and credits gold once. Guardian Fragment/book and tower
Relics are physical contestable drops, committed with the reward transaction.
A disconnected source can credit a zero-endowed persistent reward escrow;
XP attribution also survives source-body loss. This is accounting attribution,
not reconnection support. World sources mint at most642g. Ruin discovery uses
bounded actual ground traversal; eligible PvP XP has a180s pair endpoint and
300XP owner cap. Residents remain protected/peaceful according to the retained
policy; they do not become strategic contenders.

Shrines require6s quiet and a4s stationary held interaction, fully restore HP/MP,
and impose90s owner reuse without minting gold or XP. They sit outside every
tower's650cm attack range.

## Permissions and controls

Inspect the antechamber, then hold Context4s for three distinct physical keys;
Shift+Context6s chooses an owned Heartbreaker. Keys remain owned and each stores
an absolute90s reuse endpoint, merged into the persistent owner. Any key loss
revokes that lease permanently, including drop/transfer/death. Reacquisition
keeps its instance endpoint and cannot restore the old lease. Artifact commit
consumes the assembled physical item once; its lease belongs to that life and
ends on death without refund. Each contender has at most one live private lease;
different contenders can activate independently.

Both private routes warn publicly for30s, then open a60s owner lease. The public
notice carries only route/warning/open/close times at the fixed antechamber.
No owner, item identity, sheet or inventory is disclosed. Siege, retained public
Charter and private permissions combine with OR semantics. A continuous union
can cover a claim even when its original route closes.

Inspect a dropped key and hold Context3s within80cm after6s quiet. The server
reserves the exact world instance and rechecks observation, identity, revision,
life and capacity before transfer. Ordinary instant pickup cannot bypass this
hold. Inspect a shrine and hold Context4s. Fresh gestures, range and authoritative
body/hold checks govern all interactions. World interactions use IDs10–14;
retained Seal fabrication keeps its old ID9.

Every route uses the same fresh stationary10s Heart hold. Damage alone and rival
proximity preserve it; stun/hex, explicit interruption, actual displacement,
release, death, protection, lost access or the claimant's own action cancel it.
Root/silence/mute alone preserve a stationary claim. Permission does not block
rival physical entry or interference.

## Scheduled ordering

The extended world drains authority events in timestamp order, subdividing at
claim/service completion, window/deadline, combat release, status, pulse and
projectile contact boundaries. Due damage/control resolve before held completion.
Exact-close/deadline completion is inclusive; claims accepted1ms too late fail.
Ties use the unique authority ordinal assigned at claim acceptance.

Commands first drain older events; they are never backdated across a stalled
sample. Native movement has already executed when the coordinator runs. An
ambiguous changed pose conservatively cancels an older stationary hold; no
historical client trajectory is fabricated. Stationary holds drain their
scheduled endpoint. This adapter does not claim reconstructed historical paths.

Terminal resolution assigns one winner or no claim, cancels reservations and
owned temporary fields, removes pending projectiles and freezes further gameplay.

## Evidence scope

Native route and adversarial reports are separate. The normal route driver uses
real elapsed fixed-step native navigation, movement, AI, combat, earning, services
and held interactions. It does not set gold/XP/HP/items, teleport, clear towers,
reset quiet/protection or write permission endpoints. Declared starting kits
remain the baseline; the retained Laborer includes its adopted ward endowment.
Automated commands are not human input or a normal-length session acceptance.

Boundary and dedicated network lanes explicitly label their source/placement/
clock setup. They cannot substitute for the four normal route demonstrations.
The four normal runs passed380 checks with zero failures. Raw reports, travel
coordinates/times, native logs and independently replayed receipts are preserved
under `evidence/B76/Route*.{json,log}`.

| Normal route | Checks | Native game seconds | Ledger receipts | End gold |
| --- | ---: | ---: | ---: | ---: |
| Keys | 29 | 155.85 | 10 | 450 |
| Siege | 85 | 817.25 | 37 | 712 |
| Crafted Artifact | 141 | 746.30 | 56 | 566 |
| Public Charter / nonowner winner | 125 | 529.80 | 44 | 592 |

These times include their distinct earning, production and combat sequences;
they are native game time under fixed20Hz, not wall time or35-minute pacing.
All start with450 total institutional/contender gold and zero personal money.
Artifact earns eight courier rewards, buys/equips its bow, crafts two Cores,
obtains the ruin catalyst, kills the actual guardian, picks up its Fragment and
assembles/consumes the Heartbreaker. Siege kills one actual camp and all three
actual towers. Charter earns Supply/Industrial endorsements, natural STR12 and
equipped Heave Witness before paying/activating; another contender wins.

Native travel examples: West→RuinA5.05s, East→RuinB9.10s,
East→RuinC3.75s; antechamber→claim1.25s; East shrine→claim8.30s.
The Charter nonowner traverses from(-900,-1000) to Heart in3.30s during the
30s warning. These are exact recorded paths/poses with ordinary movement;
they do not promise every remote location can respond. Full travel rows remain
in each report. Boundary path queries and wall traces separately verify the
direct and covered inner flank, rather than inferring geometry from graph edges.

The19 boundary cases pass270 checks: exact close/deadline, both tie orders,
nonlethal damage, root/silence/mute, overlapping coverage,1ms late/gap rejection,
same-boundary death/stun/interrupt/displacement/release, cross-holder key lifecycle,
shrines, full finite sources, ten self-action classes and late activation.
Native path queries and wall traces pass for the direct and covered inner flank.
Boss/tower declared immunities and ordinary root/disarm behavior are exercised.

Dedicated server plus two independent clients pass13/24/24 checks at each
configured100ms and150ms RTT with jitter10. Measured echo medians are132.8ms
for both clients in the first run and182.7/182.8ms in the second. Canonical world
and inventory actors never replicate to clients; anonymous notices and current
sanitized proxies do. These are focused fixtures, not the complete B77 matrix.

All40 native rule tests pass. Native catalog/map validation reports zero errors
and five existing CrowdManager/Recast warnings. The gathered Win64 Editor build
passes with four compile workers. Affected regressions pass:403 content effects;
content network21/23/19; earned acquisition63; acquisition network17/29/25;
loadout46/27/26; geometry59/35/34; core34/18/15; V05 authority56. Each applicable
ledger is independently replayed; raw reports/logs and normalized source/import
hashes are archived in [the portable summary](evidence/B76/Summary.json).

Verification order is explicit: the four normal demos precede the localized
rejected-context target correction. Subsequent self-action, endpoint and remote
common-claim checks validate that path; no other gameplay code changed afterward.
Passing boundary cases are retained across invocations where only other fixture
setup changed. The final editor-only change corrects the stale bootstrap banner.
Source/Config were frozen throughout every Unreal operation. Earlier failed
iterations are listed in `evidence/B76/DevelopmentCorrections.json`; they are not
counted as passes.

B77 retains full100/150ms/privacy and human matrices, rendered/physical input,
OS scale, cold cook/package, long1/4-player sessions, restart/performance and
inherited V06-D01–D08 acceptance. No full v0.7 delivery is claimed by this slice.
