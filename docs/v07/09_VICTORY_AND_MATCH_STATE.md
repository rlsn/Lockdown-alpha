# Grand Heart access, claims and match endings

## Shared outcome and authorization

One server-owned Heart state, one winning receipt. Having wealth, keys, an artifact, kills or a surviving body never wins automatically. These routes grant permission to begin/finish the SAME physical ten-second claim. The protected Heart barrier is claim authorization, not a collision wall preventing opponents entering/interfering. A defender needs no access route to attack, scout or interrupt a claimant.

Authorization for contender C at server time T is:
- SiegeOpen (all three towers destroyed), OR
- PublicCharterOpen with start<=T<=close, OR
- live personal KeysLease(C) with start<=T<=close and all three distinct key IDs currently owned/carried, OR
- live personal ArtifactLease(C) with start<=T<=close and current same life.
All still require alive, unprotected, active contender and nonterminal match. Access is OR, never a requirement to finish every route. No private Charter flag. All route IDs are scoped to match, owner and life where appropriate.

## Route state machines

| Route | Progression / commit | Warning / open | Death / loss |
| --- | --- | --- | --- |
| Siege | Destroy GrandTowerA/B/C; each persistent destruction receipt | Immediate permanent global access after third destruction | Never restored by death/rejoin |
| Charter | v0.6 Witness + Supply/Industrial endorsements +100 gold registered purchase; four-second terminal activation | Global30 s warning,60 s open for EVERY contender | Before activation registration retained, ready at actual respawn+30 s; after activation warning/window continues, spent stays |
| Keys | Own three unique keys; quiet four-second activation at antechamber, within120 cm/sight | Public30 s warning of a personal breach, then60 s owner-only authorization | Keys are NOT consumed; dropping/transferring/losing any key revokes lease immediately. Death drops keys and cancels lease |
| Heartbreaker | Assemble physical item, quiet six-second antechamber activation consumes it atomically | Public30 s warning of a personal breach, then60 s owner/same-life authorization | Before activation item drops; after activation death cancels lease, consumed artifact is not refunded |

Only one live private warning/lease per contender, across key/artifact routes. Public Charter has its existing global one-warning/window concurrency, no overlap/queue/extension. Private leases may overlap each other and Charter/siege; there is no global lease lock a rival can monopolize. An overlapping route never extends another's timers. Authorization may seamlessly continue via another still-valid route; an ongoing claim is not cancelled merely because its original route closes while another authorization remains valid.

Keys use shared90-second reuse cooldown on all three instance IDs AND owner, set at activation commit. Transfers preserve the maximum endpoint; reacquiring cannot restore a revoked prior lease. A new activation needs all keys, cooldown expiry and no live lease; it creates a fresh warning. Key activation reserves the three instances during its four-second hold but mandatory death drops release locks first. After commit they remain ordinary cargo. Do not consume/clone/reset the keys. No extra key spawn after loss; dropped instances persist until picked up or match reset.

Key/artifact activation needs alive/unprotected, six seconds quiet, stationary hold,120 cm/sight and sufficient deadline for FULL warning+opening. Activation completion+30+60 <= deadline. Begin rejects if that cannot hold at its scheduled completion. Charter purchase retains v0.6 current-time+4+30+60 feasibility check; delaying later can waste an unused registration, explained before payment. No refund. Siege has no warning/activation deadline requirement; the remaining time may simply be insufficient for ten-second capture.

Public route message exposes route type, static precinct location, exact warning/open/close times and broad activation event. It does not expose hidden approach/body coordinate, private keys inventory, class or persistent identity. Active Heart attempt intentionally has a public location/timing sensor tied to the static objective; concealment breaks and cannot mask the attempt. Defender targeting still requires ordinary authorized observation of the actual body.

## Claim state machine

Idle → Channeling(claimant, life, start, scheduledEnd) → Acquired OR Cancelled. Multiple contenders may hold independent attempts concurrently. No passive240 cm contest check remains: merely parking a body cannot block victory. Earliest valid scheduled completion wins; exact ties use the role-neutral server claim-start ordinal allocated at acceptance (no human/NPC priority). Log start ordinal and result.

Begin requires valid authorization now and predicted authorization through scheduledEnd, alive/unprotected, claim radius120 cm, no stun/hex, fresh held input and no other own action. Root/silence/mute alone do not prohibit the generic Heart interaction. Clear movement orders and end concealment. The claimant cannot attack, cast skills, use items, swap loadout, traverse gates or perform another service/channel during claim; requesting one explicitly cancels the claim first. Pre-existing barriers/dome/wards/traps may remain; no invulnerability is granted. Focusing a menu or losing hold cancels. Movement input inside the circle also cancels as deliberate self-cancel; forced nonzero movement always cancels even if destination remains inside120 cm.

Stun, hex, death, actual forced displacement, explicit Interrupt, leaving the circle, hold expiry, protection activation, life change, access loss or terminal match cancels. Ordinary damage does NOT cancel, even if absorbed; lethal damage still kills. Silence alone does not cancel Heart, but Counterpulse does through its explicit Interrupt operation. Mute and root alone do not cancel. Barriers do not stop control/interrupts. An enemy within range must act. A stunned claimant cannot self-activate a cleanse; another consenting unit may strong-dispel, but the interrupted ten-second attempt does not resume from prior progress.

On cancellation reset progress to zero, retain spent route/item resources, publish only authorized reason. Restart is a fresh hold and full ten seconds, with authorization rechecked. No banking partial progress. Claim completion at opening close/deadline is legal if scheduled completion equals that endpoint and all other conditions survive the same batch. Claim begun too late to finish within a known finite authorization is rejected, unless permanent siege access exists.

## Server event ordering and ties

Use monotonic server event time. Simulate native movement to the next due event boundary, determine due combat/effect expiries, apply simultaneous combat, then finalize deaths/reservation release/objective loss. Next apply scheduled institutional events and legal noncombat commits, including tower destruction/access and activation. Finally validate due claim completions against access intervals at their SCHEDULED time, then deadline/no-acquisition. Ordinary late client commands are never backdated.

At exact close, set the route closed for future starts but evaluate a due completion at close using the inclusive interval. Do not invalidate that completion by reading only a now-false boolean. At exact deadline, due lethal combat/disable still cancels before a scheduled completion; a surviving valid completion wins before NoAcquisition. A post-deadline sample cannot add window time. If tick stalls, drain due server events in scheduled-time order rather than evaluating all against one late wall-clock timestamp. Native movement/collision is still authoritative; never reconstruct an invented client path to justify an old claim.

Examples: open[100,160], claim150→160 may succeed;150.001→160.001 rejected. Stun at159.999 or160 cancels completion at160. Damage80 at160 leaves claimant alive: does not cancel. Two valid claims completing160 tie by start ordinal. Key transfer at159 before completion160 revokes its owner's lease unless siege/Charter independently authorizes. Artifact activation followed by owner death during warning gives no refunded item/lease; public Charter opening survives purchaser death.

## Match end

Winner receipt transitions match Active→Acquired exactly once and freezes new gameplay/economy/route commits. Cancel/release all remaining sessions and holds without generating replacement items. At deadline without a valid due winner, Active→NoAcquisition, never a hidden score or last-survivor winner. Existing withdrawal/disconnect/respawn policy remains; dropping the last connected human does not crown someone. Reset closes item/gold ledgers under Reset and creates next match state once.
