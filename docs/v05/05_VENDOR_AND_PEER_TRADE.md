# Vendor and peer trade

## Vendor purchase

At the Guild, use the same inventory shell for the public catalog and own bag. Open through a visible service interaction within 120 cm, alive, unprotected and with body line of sight. Opening stops movement and uses a major nonpausing panel. Leaving range, losing sight, damage, death, disconnect or match end closes it. No remote buying from the minimap.

Retain Step, Spark, Patch and Parley book definitions at 40 gold each and unlimited public stock if those defaults were delivered. Quantity is one unique instance per Buy request; no stack or bulk-purchase workflow is required. Preview the definition, taught skill, real read duration, own knowledge status, price and capacity. Already-known books may be bought intentionally for trade, with a clear Already learned label; they cannot be read for an extra rank.

Buy validates public vendor/catalog version, request ownership, current balance, life, range and free capacity; deducts exactly once and creates exactly one owned item. Failure changes nothing. A stale quote refreshes the displayed price before a new confirmation. Do not charge a newly changed price on an old click. Update only after acknowledgement; repeated clicks while pending do not submit new purchases. Intentionally buying another copy after acknowledgement requires another explicit click.

No resale or trade XP. Do not add a Sell tab or inert button for an unimplemented operation. Existing paid work and crafting are distinct services using the same components where appropriate, not fake vendor transactions.

## Peer invitation and information

T or the inspector's Offer trade action requests exchange with an observed nearby humanoid capability. Require both alive, unprotected, within 120 cm, own-body line of sight, no pending action, no other invite/session and no positive damage dealt/taken for six seconds. Invite only after a gesture; do not broadcast on selection.

All visually interchangeable humanoids expose the same neutral Offer trade affordance. No capability flag, actor subclass, rejection or latency UI identifies human versus NPC. A human may accept/decline; a simple resident policy can decline with the same ordinary Unavailable response. A trained AI trader is not required. Apply server scheduling to generic unavailable responses, proposed next 250 ms response bucket regardless of reason; never disclose diagnostic reason codes to the initiator. Human acceptance is voluntary behavior, not a claim of perfect indistinguishability.

An invitation expires after 30 seconds. Receiver sees a neutral observed sender label and Accept/Decline. Repeated outgoing attempts to the same body are rate-limited to one per ten seconds by stable server identity, including declined/expired attempts. A session expires after 60 seconds without a valid new offer/consent change and has a hard 180-second lifetime. Replayed requests and polling do not extend timers. Cancel if either loses required body observation or eligibility.

Accept opens the same peer-exchange shell on both clients. Each sees its own bag/balance and both explicit offer trays. It sees only the other side's offered item definitions/instances, quantity one and offered gold, never the other bag, exact wallet, build or private economy. A temporary trade-scoped handle identifies each offered item to the other party; do not expose permanent contender, wallet or full inventory IDs.

## Offer and consent state machine

States are Invited → Draft → Review → Committed, or Cancelled/Expired. One session contains at most six offered items per side plus a nonnegative integer gold amount. No global currency, skill, Witness, credits or item owned by someone else is eligible. Books, charges and a Seal may be offered; final capacity and one-Seal limit still apply.

Each accepted edit creates a new authoritative offer revision and clears both Ready and Confirm flags. An edit includes changing gold, adding/removing an item or any relevant offered item mutation. Reserve offered physical instances within the trade so neither participant can use, drop, offer elsewhere or double-sell them. Reservations do not change owner or remove carried capacity. Quick references remain present but locked; cancellation releases them.

Gold remains the real wallet balance, not protected escrow. Theft and death cannot be evaded by typing a gold offer. Any wallet mutation while drafting/reviewing creates a new offer revision, invalidates both approvals and refreshes only the owner's balance. Keep an invalid offered amount visibly invalid until its owner edits it; do not silently change terms. The partner sees Offer needs review, not the hidden balance or cause. Other accepted outgoing gameplay transactions cancel trade before executing. Incoming theft retains its normal bounded transfer and RNG rules.

Each side presses Ready for the exact current revision after authority validates its offered ownership, amount and projected final capacity. When both are Ready for that revision, enter Review. Show both directions, gold totals, six item rows at most, and a prominent final exchange summary. Require a separate Confirm button from each side for the exact session ID, revision and server-computed offer digest. The second valid confirmation commits atomically. An edit from Review returns to Draft and clears all consent. No timer or initial invitation can auto-confirm a changed offer.

Cancel or Escape from the trade modal cancels the session for both and releases reservations. A definitive terminal acknowledgement wins over a late local cancel: report the actual committed receipt, never claim the exchange was rolled back after completion. A disconnected participant cannot leave items or gold in limbo.

## Atomic commit and conservation

At second confirmation recheck both live identities, active session, exact revision/digest, body range/sight, protection/quiet state, item ownership/reservations, gold coverage, integer overflow, projected bag capacity and type limits. Transfer all items and gold in one authoritative transaction, update both inventory revisions and clear transferred quick references, then issue one result receipt. If any validation fails, transfer nothing and either return to Draft with refreshed validity or cancel for lost eligibility.

Projected capacity accounts for outgoing items. A full 24-slot bag giving two and receiving one ends with 23; it is legal. Giving one Seal and receiving another can be legal if the final one-Seal count is one. Do not reject using only the pre-exchange occupancy. All definitions are nonstacking. Arithmetic uses bounded signed integer validation before deduction; reject negative, excessive or overflow amounts.

For example, wallets 100 and 20 offering 40 and 5 finish at 65 and 55. Sum 120 is conserved, without a fee. A no-item gold gift is legal with both confirmations; a completely empty exchange is rejected. Trade grants no XP, credits, Witness or new craft allowance. Trading away a crafted Seal does not reset its crafter's one-craft-per-match counter.

Wallet mutations, theft, death, trade and item use serialize in the same authority transaction queue. Process due damage/death before trade completions, consistent with the retained batch ordering. Death releases reservations before gold loss and physical drops; it cannot occur halfway through a trade transfer. There is no special trade invulnerability or hidden safe mode.

## Required visible states

Use clear stages: Invitation, Editing offer, Waiting for other side, Review exchange, Confirmed/waiting, Completed, Cancelled, Expired. Show who still needs to act without identifying a hidden class or player role. Disable use on reserved items and explain why. A changed offer visibly resets approvals. A receipt shows actual sent/received items and gold once; a timed-out screen does not invent a successful result.

No text-chat or out-of-game message system is required. A vendor and a willing participant are sufficient for normal demonstrations; fixture participants do not replace the required two-human usability evidence.
