# Inventory and quick items

## Carried container

Propose one 24-slot bag per unit with the common item capability, displayed as a six-by-four grid on the reference desktop layout. Each slot holds one unique physical item instance. No stacks, weight, equipment bonuses or nested bags are introduced. Gold is a separate nonnegative integer balance. Capacity and counts are data, not assumptions baked into widgets.

Owner can open its full bag. Selecting another unit never opens that unit's bag. NPCs use the same authoritative carried-item schema where applicable; policy may differ, not ownership legality. Residents are not required to receive speculative random equipment.

Supported items are unread books, finite ward charges and one carried Concord Seal. Retain actual starting supplies and prices from the audited baseline; do not seed new wards or books to fill the grid. Each old finite ward charge becomes one unique charge instance. Learned knowledge, attributes, skill points, Witness and work credits have no item instance and cannot be dragged, dropped or traded.

The four-unread-book cap is explicitly superseded by general capacity. Up to 24 books may occupy a bag when no other items are present. A Seal counts toward the 24 and retains its one-per-carrier limit. Books and wards have no additional per-type cap. The new space is a proposed interface experiment, not proof of long-session economy balance.

## Identity and ownership

An item record has ItemId, definition ID, owner/container and slot or dropped location, state, and revision. It has exactly one authoritative location. Sorting changes slot order without creating new IDs. Owner-private quick references resolve ItemId, never an array index or widget pointer.

Reorder, bind/unbind quick item, purchase, pickup, read, ward placement, drop and trade use expected inventory/item revision and a request ID. Validate on authority and return the committed state or rejection. A repeated request returns its receipt. The UI may show a pending ghost but cannot duplicate an item locally or execute it before ownership is acknowledged.

Reorder and quick binding are non-gameplay organization and may commit while the inventory major panel is open, alive or dead, provided the item is still owned and unlocked. No preparation station is needed to assign item shortcuts. This does not change the station requirement for learned-skill equipment. Reordering does not clear availability or complete a use channel. Focus/death clears pointer drag, not the retained item's identity.

## Quick references

Provide four owner-private quick slots, assigned by drag or a click action Assign to slot. A usable instance can be assigned to at most one quick slot; moving it swaps or clears the old binding atomically. Quick slots add zero carried capacity. Sorting or moving the item within the bag preserves its binding; ownership loss, consumption or drop clears it in the same commit.

Books and ward charges are quick-eligible; Seal is not. No quick reference to a learned skill, another unit's item, a dropped item or a reserved trade item is legal. A book shortcut executes the real stationary three-second read with retained quiet-state checks, duplicate-knowledge rejection and no automatic equipment. It never casts the taught skill. Holding the shortcut key starts/maintains reading. Inventory Use arms the same read and asks Hold F, without requiring both inputs.

Ward shortcut arms placement. Left-click a legal ground point confirms; right-click/Escape cancels without consuming. Retain actual v0.4 range, body visibility, lifetime, HP and source ownership, proposed 96 cm placement, 560 cm sight and 60 seconds if unchanged. Consume the charge only when placement succeeds atomically; reject without an item loss if source creation fails. No cost on preview. The new source cannot be duplicated through shortcut and inventory calls racing.

Dropping, consumption and trade preserve applicable cooldown/availability identities. A different shortcut cannot bypass a per-item or retained contender-wide limit. If the actual baseline has an item cooldown, keep its authority rule; do not invent a zero-cooldown alternate entry point.

## Inventory operations and feedback

Show capacity, own gold, search, All/Books/Wards/Objective filters and a selected-item detail pane. Filters and search are local views; they do not reorder or alter a trade offer. Sort by type then definition name then stable ItemId. Show empty slots and locked/reserved items distinctly. Offer keyboard-focusable Use, Assign, Unassign and Drop buttons as alternatives to dragging.

No drag gesture drops an item into the world. Drop requires an explicit button and confirmation naming the item, especially a Seal. Authority requires alive, unprotected, no active use/trade reservation and legal ground near the body, proposed 80 cm maximum. Use the retained valid-ground/Guild fallback only for mandatory death drops. A voluntary drop with no legal location is rejected, never silently moved to the Guild.

Use and Drop controls can be armed in the panel, but actual use exits the major panel and requires fresh explicit world confirmation/hold. Closing a panel never casts by itself. A drop confirmation is itself a major modal and atomically commits only after its explicit confirm; it requires current ownership and legal life. Do not require a hidden click through the panel to finish.

Purchase and pickup require a free bag slot and all type limits before charging or transferring. A full bag gives Inventory full with no spend. Book reading teaches rank one and consumes exactly the owned unread instance at commit; knowledge/ranks persist under retained death rules. Duplicate knowledge retains the book and consumes no reading time/cooldown.

## Death transfer and cleanup

Unread books and Seal retain their v0.4 death-drop behavior. Ward charges follow the audited baseline; if it had no physical-charge death policy, the proposed v0.5 rule is to drop carried charges with the other valuables. The [local audit](B50_BASELINE_REVIEW.md) found explicit loss of unused charges on death and no return grant: retain that rule by destroying carried charge instances once and clearing their quick references, rather than adopting the drop fallback. Normal physical item transfer never creates free charges on return. All dropped quick references clear, knowledge and skill slots persist, and the corpse's 20-second cleanup does not delete dropped item ownership records.

For the proposed drop-all-carried policy, the bag returns empty after death unless the audited baseline explicitly retains an item definition. No fresh supplies are granted on respawn. Every death transaction releases trade reservations first, applies the retained gold loss, and drops each applicable item once. Trade cannot intercept or duplicate a death drop.

No inventory persistence across matches is added. Restart resets normal match ownership, quick references, dropped items and all transaction sessions. No live v0.4 save conversion is required.
