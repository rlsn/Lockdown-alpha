# Interaction control override — 28 October 2026

This records the user's current design decision and supersedes earlier master,
v0.9 and inherited instructions that require a contextual interaction key or
physically holding an input. The supplied specifications and historical evidence
remain frozen as provenance; their old control acceptance is superseded.

## Sources of interaction

There is no generic Interact key and no object-specific keyboard shortcuts for harvesting, chopping,
opening, carrying, breaking, ignition or traversal. An action comes from a real
prepared ability, a physically owned/bound item, ordinary basic attack, or an
eligible POI whose behavior begins when the character stands at it.

An object-specific shortcut must not return under a new name or through a free
generic "interact ability". Prepared-action and physical-binding limits remain.
Inventory selection, binding, transfer and transaction UI operate on exact
items; they do not grant unbound combat actives.

Right-click approaches and picks up an observed ordinary physical item, or opens/closes an ordinary door. Left-click opens storage and trader interfaces, approaching first when needed. The same click must not also issue a ground order.
After pickup, its active use comes from its item slot. Basic attacks can target
breakable objects. A tree-cutting or harvesting active must have an actual
cataloged item/ability source; the old free F/Z actions are not substitutes.
The user's Quelling Blade/Tango examples illustrate this principle and do not
by themselves add those named items, prices or world endowments to this game.

Services and installed traversal can be eligible POIs; ordinary doors and storage use the pointer mappings above. Specialized lock work and resource extraction need their
defined sources. Merely standing nearby must not silently consume a valuable
item, buy something or confirm an irreversible transaction.

## Channels are orders

Press or click once to issue the action. The character continues its channel
without a held keyboard or mouse button. Releasing that input does not cancel.
Show the action and progress, preferably through an authoritative progress bar.
S stops movement and cancellable actions. Movement, damage, control, loss of
eligibility, focus/lifecycle changes and explicit superseding orders retain
their category-specific interruption rules.

Network heartbeats remain implementation liveness checks. They are maintained
by the active command rather than a physical key-down condition. Current UI
profiles tolerate gaps shorter than two seconds; expiry releases reservations
and a late refresh cannot restart the order. S and gameplay interruptions do
not wait for that timeout. See [native liveness evidence](B91_LIVENESS.md).
A stopped automatic POI must not restart every
frame: rearm it only after leaving/re-entering or issuing a new deliberate order.
Automatic POIs wait for a stationary character without a pending or active
movement order; walking through their radius must not stop the player.

## Current migration

The B91 prototype's F/Z/E/R/V/B/H/O/K/C/J/P/X/T/Y/L/N object shortcuts are
replaced by ordinary navigation, basic attack, prepared ability/item inputs,
item UI and POI behavior. Six shared actions and four accessories retain their
normal command bar positions. E/R are shared ability slots, not tree/torch keys.

Old held-channel fixtures are backend history, not evidence that the revised
player controls are complete. New acceptance must prove one press followed by
release, uninterrupted completion, progress feedback, S cancellation, no
automatic restart, source validation and the existing privacy/cost guarantees.

## Approved finite forestry source

The user explicitly selected adding a forestry item now. The B91 overlay adds
11 Standard Forestry Knives, one finite initial ground instance per settlement.
Each weighs2, occupies a real accessory binding, gives no passive stats, and
provides targeted Harvest and Chop. Both require6s quiet and a4s channel;
Mute blocks item use, Silence alone does not. Harvest creates exactly one Fiber
from the named living sapling; Chop clears without material. The reusable knife
is reserved during the channel and retained on cancellation. There is no shop,
refill, respawn grant or added preparation slot. The supplied80-item catalog and
59 protected release files remain unchanged; the overlay records the addition.
