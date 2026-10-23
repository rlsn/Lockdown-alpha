# B61 third slice — physical Treasury banking

Historical checkpoint. [B61 completion and current verification](B61_IMPLEMENTATION.md) supersedes the pending status below.

23 October 2026. Physical banking is now implemented in native source. B61
remains partial because full retained-item causal coverage, physical-world
reconciliation and match-close export are still pending. B62–B67 remain open.

## Behavior

City Treasury shares the City's existing Guild service position, using the
actual region's site lookup in both saved topologies. The public landmark now
reads GUILD / CITY TREASURY. This is an adopted co-location for the small
prototype, not a new ordinary NPC identity. The existing noncombat/nonlootable
landmark supplies the service; ordinary bodies retain their masking policy.

Open Bag and choose **City Treasury bank · at Guild** within 120 cm. The server
requires an active living unprotected contender, actual body sight, clear
segment, six seconds without dealing/taking positive damage, no combat action,
preparation or objective channel, and a playing match. It cancels peer exchange
and invalidates the bookshop. A bank session is private to its controller,
match and life; damage/dealt damage, eligibility/range/sight loss, another
service, closing the bag or focus loss invalidates access. It never pauses play.

Type whole gold, choose Review deposit/withdrawal, inspect exact gross, fee and
net credit, then separately Confirm. Changing amount/session/balance revisions
invalidates review. Pending requests block further inventory/vendor/trade/bank
submissions. Confirmation sends expected wallet and bank revisions, never a
client-computed fee. Incoming theft leaves wallet gold exposed and makes stale
bank confirmations reject. The server independently revalidates access and
balances when its queued commit is processed after damage/death resolution.

Successful banking uses the same causal ledger transaction as its exact
persistent owner-state update. Deposit100 debits100, banks98 and burns2;
withdraw30 from that bank leaves68 and credits30 to the wallet. There is no
interest, loan, remote transfer, withdrawal fee or ordinary-spending fallback
to the bank. Bank gold is owner-only persistent contender state, survives death,
and retires alongside the disconnected wallet without reducing aggregate gold.
Theft/death code continues to debit only carried Gold.

The bank session rejects stale life/session/revisions, bad integer amounts,
insufficient/overflow/no-credit cases. Per-controller monotonically increasing
requests, pending duplicate suppression and cached final receipts prevent
repeat commits. Cached receipt eviction cannot re-enable old IDs. Opening and
submission use bounded queues and token rates. Terminal matches reject queued
commits. The bank owns no inventory reservations or protected wallet escrow.

## Verification

Both incremental Editor builds **passed** on UE5.8.4 CL58902052,
MSVC14.44.35229, SDK10.0.26100.0, with MaxParallelActions4:

- `Artifacts/20261023-135140-969-BuildEditor/BuildEditor.log`: 31 actions,
  165.98 seconds; reflected owner/session fields and all integration compiled.
- `Artifacts/20261023-135458-922-BuildEditor/BuildEditor.log`: seven actions,
  11.09 seconds; final request/deadline/service-switch refinements compiled.

`git diff --check` passed. Source/Config stayed frozen during each build.
The existing native EconomyAccounting tests cover the shared bank arithmetic,
revision checks, replay behavior and aggregate accounting; they compiled in the
previous slice, but no execution result was obtained. This slice does not
repeat the previously timed-out rule launch or start a long scenario.

No new network/native-input/access-race/OS-scale/layout acceptance is claimed.
The new UI still needs on-screen human review and actual physical banking with
normal earned gold. Source compilation does not certify E41/E42. No package or
25-minute session was run.

## Next

Complete retained physical item receipt coverage and world/journal comparison,
then B62 paid material supply and regional commerce. Treasury Charter split
checkout belongs to B65 and is not implemented by these bank controls.
