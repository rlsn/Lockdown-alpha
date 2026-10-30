# B92 — physical crafting and procurement

30 October 2026 · B92 remains **in progress**

Native source **0d93aeec8a11f657409bf3b4cd8ad64fca788fd7**. [Frozen evidence](evidence/B92Work/README.md)
extends [prepared Survey](B92_SURVEY.md).

## Delivered work

LMB opens a physical workshop or contract counter. Confirm a displayed recipe
once; the character channels with a progress bar until completion or interruption.
Releasing the button continues. S, closing the counter, movement, loss of access,
damage/action changes, life or controller changes cancel without consuming inputs,
charging a fee or spending an order quota. There is no new interaction key.

| Work | Endpoint | Actual input | Time | Result |
| --- | --- | --- | --- | --- |
| Toolkit | Five city and six town workshops | 2 Fiber + 1 Ore; 4 carried gold | 4s | 1 physical Toolkit, no XP |
| Core | Same workshops | 2 Toolkit + 1 Crystal; 6 carried gold | 6s | 1 physical Core, no XP |
| Core procurement | Capital Treasury | 1 Core | 3s | 80 gold, 60 XP, first Industrial endorsement |
| Supply commission | Capital/Riverport guilds | 2 Toolkit | 3s | 50 gold, 30 XP, Supply endorsement |

Basic crafting uses full inherited fees. Analyze discounts, equipment work orders,
shortage orders and courier contracts are separate integration work. Service
selection follows the protected endpoint roles; towns add workshop access without
adding commodity pools or cash. Treasury retains its commodity/bank panel and
offers a separate contract view. Other workshops/guilds expose only selected work.

## Authority and conservation

The request names exact unbound, unreserved bag identities and revisions, the
current life/session, bag/wallet revisions, work terms and fee. The server checks
body reach, floor, sight, quiet/protection/status eligibility, stationary grounded
movement, free hands, capacity, exact recipe, funds and available quota. Accepted
work reserves those same actor/bag instances; it does not escrow duplicate goods.
Build, body, bag, wallet and input revisions are checked again before commitment.

One canonical transaction consumes the inputs and either creates the output with
its causal parents and burns the fee, or mints the named procurement reward while
spending both global and persistent-seat budgets. Core allows three deliveries per
seat and 72 total; Supply allows one per seat and 24 total. Death or controller
replacement does not refill these existing budgets. Only successful delivery grants
XP and the private endorsement receipt. Duplicate start/completion confirmations
retain the same receipt and cannot create a second channel or payout.

Reservations release on interruption; input revisions advance so a prior preview
cannot be reused as fresh work. A stale start preview or exhausted quota refuses
without silently accepting different terms. Independent accepted channels recheck
their actual recipe, fee and remaining quota; another contender finishing work
does not cancel a valid craft through a global quote revision. Local panels expose the owner's terms,
not remote bags or private builds. Source inputs consumed by a recipe also close
their original finite-world conservation records.

## Evidence and limits

The connected suite passes **83 checks**. Independent replay closes **295 receipts, 149 accounts, 635 historical identities (610 live), and 3,202 gold**:
3,288 start + 130 named procurement mint - 216 burn. Burn comprises 184 official
purchases, 1 bank fee, 26 crafting fees and 5 funded institutional production.
Five Toolkit crafts and one Core craft consume 18 input instances and create six
outputs with causal parents; the two deliveries consume three finished goods.
Exactly two procurement receipts spend their shared and personal quotas and
award 90 XP. Four actual automatic extraction channels and two Survey channels
share the same 54 total source charges; the remaining drain stays a fixture.
Two remote owners finish concurrent Toolkit channels without invalidating each
other. A labelled authority fixture extracts three existing finite charges for
the second owner; it grants no new allowance or money. The 720p capture verifies
recipe terms and the Core progress bar.

Rules59, economy125, four-controller roster37, B91 source/control53 and
full-map combat50 pass. Both full-unity targets pass. All207 protected assets
and spec inputs remain unchanged. The concurrency refinement reran connected
services and both targets; unaffected regression evidence is retained.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/RunB92Economy.ps1
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/RunB91Combat.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather
python -B Scripts/ExportB92WorkEvidence.py docs/v09/evidence/B92Work --verify
```

Travel, prepared Survey attributes and cache input transfers remain labelled
fixtures, as does the earlier conserving 400-gold reserve transfer. Actual work
channels and delivery rewards execute through normal remote owned requests. This
proves the transaction route; it does not prove a naturally discovered, affordable
or well-paced zero-kill progression journey. Witness, registered Charter, actual
book learning/preparation, courier lifecycle, procurement death/race stress,
all-endpoint walks and human readability remain open.

The initial unity build failure is retained: adding the new source changed unity
grouping and exposed an existing river-probe variable name collision. The local
probe name was made specific without changing its behavior.
An initial connected run completed Survey on authority but sampled the client
bag before the owner snapshot arrived. Successful Survey checks now wait for
the received identity with a bounded timeout; the failed run is retained.
Subsequent diagnostic failures exposed insufficient post-sale Ore, a stale
client baseline after cache transfers, and insufficient post-craft cached Fiber
for the full-bag regression. The completed scenario obtains one more Ore through
a normal automatic channel, delivers fixture snapshots before measuring changes,
and fills capacity from already-accounted cache/ground Fiber. No missing inputs
are replaced with a free grant. The failed reports and authority logs remain
separate from accepted evidence.
The first rules run also caught the service payload allowlist still describing
the pre-work schema. The explicit allowlist now includes only local work offers,
their revision and the requesting owner's input stamps; serialization preserves
those exact inputs and changed identity is not an equal retry. The rejected
automation report/log is retained. This test-only correction does not change
the connected runtime behavior already exercised above.

Next: physical courier work, authored full-map Witness and earned zero-kill Charter
progression. Routines/security/hostiles, legal contender AI/tier transitions,
authenticated reconnect and full recovery remain open. `v09_full` remains closed;
B93/B94 acceptance is separate.
