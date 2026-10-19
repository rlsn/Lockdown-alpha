# B35 representative interaction checkpoint

The uncooked migration slice now includes one repeatable courier route, real Seal
ownership and theft, both Heart access routes and a single objective-only result.
Native prompts, confirmed progress, public site markers and a labelled local-host
fixture menu make these paths available without console commands.

## Work and interaction ordering

E selects a visible dropped Seal within 80 cm if the carry slot is empty, then
Heart within 120 cm, then the nearest public civic site within 120 cm. Authority
rechecks current life, protection, combat, range and clear ground. Requests carry
match/life/request generations; the controller caches 32 retry receipts and limits
edges separately from 10 Hz unreliable held-state refresh. Missing refresh expires
after 250 ms. Release cancels the token so a late heartbeat cannot revive it.

At Guild E directly accepts one private West-station job. Three stationary held
seconds collect a package; three more at Guild commit 25 gold, 15 XP, one commission
and one delivery serial. Replays cannot pay twice. Acceptance repeats after twenty
seconds. Death cancels the unfinished job/package while retaining completed XP and
commissions. Two deliveries reach the retained level-two threshold; upgrade offers,
other destinations, full rank progression and crafting are deferred.

ALDCombatDirector calls objectives after all due damage, deaths and returns. Legal
combat, damage, movement intent, physical displacement, release, range loss and
stale hold reset channels. Work commits before claims and deadline evaluation.
Damage in a channel's completion batch prevents that completion.

## Seal lifecycle and privacy

One nonreplicated server item has an immutable identity and exactly one of three
states: dropped, carried or consumed. The local authority's non-shipping scaffold
fixture seeds at most one item; no remote fixture RPC exists. Pickup uses the
connection's currently permitted observed ID and rechecks its position and empty
carry slot. Requests in the same authority batch are shuffled with an independent
private RNG. No fixed connection/role priority is imposed.

Only dropped items have observed poses. Carried inventory stays on the owner;
no carrier role/body marker is added. Broad public district notices carry no
identity or coordinates. Death and logout drop the same item on nearby validated
navigation ground; if unavailable, Guild is the logged fallback. Corpse cleanup
never deletes the item. Protection and stale-life requests cannot recover it.

## Heart and terminal state

All four defensive flags clear opens ordinary Assault access. An actual carrier
can instead use the Seal route while flags remain. The route stays fixed for that
attempt. Another living, unprotected contender within 240 cm resets claim progress,
regardless of access or held input. Residents do not contest. Public aggregate
claim/contest/route/progress contains no claimant identity. Held attempts contribute
shared 360 cm high-level Heart sight with two-second linger.

Ten uncontested seconds commit one terminal receipt. Seal-route victory consumes
the item; an ordinary victory does not. A scheduled completion at or before the
twelve-minute deadline may win when evaluated on a late sample, provided it is
still legal. A later scheduled completion cannot. Otherwise the deadline yields
No claim. One or zero active contenders never automatically wins.

Result state freezes movement, combat, returns and transactions. Rule-time sight
sources freeze as well; replication continues. Post-result departure removes
connection bookkeeping without another ownership transaction. Full in-world reset
and rematch isolation remain B36.

## Fixtures and verification

The host menu explicitly separates setup fixtures: shared sensor toggle, four
independent defensive flags and one Seal seed. They require local authority and
the non-shipping scaffold profile and are logged. These provide acquisition/access
setup only; tower combat and Seal crafting have not been ported.

Run Scripts/RunObjectiveCheck.ps1 for real-actor injected-clock checks, including
completion-batch damage, deadline boundaries and logout cleanup. Run
Scripts/RunObjectiveNetworkCheck.ps1 for a separate authority and two owning-client
processes. Actual RPCs perform courier work, shuffled recovery, lethal melee,
theft, natural corpse expiry/return, contest, stale hold and final claim. The probe
uses fixed poses, a lethal-HP setup and a deliberate one-batch pickup pause.
Measured echoes and acknowledgement samples are diagnostic, not the complete
performance gate. See TestReport.md for executed evidence and remaining limits.

Three public native landmarks use engine primitive meshes/materials; the existing
23 generated packages remain unchanged. Cooked clients, reset/isolation, broader
latency/performance and complete human review are B36.
