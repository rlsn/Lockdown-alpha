# Input navigation and camera

## Default bindings

All labels derive from one mapping registry. Keyboard/mouse rebinding, conflict detection, reset and saved local preferences are required. Reject same-context duplicate bindings rather than silently overriding. Reserve Escape as a guaranteed back action. Gamepad is deferred.

| Input | Idle-world result |
| --- | --- |
| Left-click unit | Select that currently observed unit |
| Left-click terrain | Return inspector to self; no attack or move |
| Right-click walkable terrain | Replace the current move destination |
| Right-click observed unit | Approach its observed position; never attack or follow indefinitely |
| Right-click station or dropped item | Approach a legal interaction position; do not auto-use |
| A, then left-click | One explicit basic attack intent using retained melee rules |
| Q W E R | Skills in equipped slots one through four |
| 1 2 3 4 | Quick-item references one through four |
| Hold F | Perform the eligible selected or focused contextual channel |
| S | Stop movement, clear destination and cancel an interruptible channel |
| Hold Shift | Sprint while movement is allowed; not an order-queue modifier |
| I | Inventory |
| P | Character development/preparation |
| T | Offer trade to the selected observed nearby body |
| Arrow keys or edge pan | Pan the camera |
| Hold Space | Follow/recenter on own body while held |
| Home | Toggle follow camera; show the current mode |
| Mouse wheel | Zoom if over the world; scroll if over a panel |
| F1 | Help and controls |
| Escape | Cancel the topmost transient state, then close the topmost panel, then menu |

WASD has no default locomotion binding. Space never invokes Step. F never directly consumes a ward. No shift-order queue, attack-move loop, multi-selection or possession of another unit is required.

## Orders and physical navigation

Use the current grounded CharacterMovement component and compatible native navigation. Right-click emits a destination order, not continuous click spam, a teleport or a client-authoritative path. A new order supersedes the previous one by monotonically increasing order ID within match, contender and life generation. Duplicate acknowledgement does not restart a route. S creates a new cancellation epoch; an old delayed route acknowledgement cannot resume movement.

Ground acceptance radius is 40 cm, excluding capsule radius. Project a surface hit no farther than 100 cm horizontally and 50 cm vertically on the intended walkable surface. A click on a cliff or roof must not snap to a different 200 cm plateau. For minimap input choose the authored surface for the map cell, revalidated by authority. Reject ambiguous/nonwalkable targets clearly; a partial path is rejected rather than reported as arrival.

Right-click approach uses a snapshot of a currently authorized target position. Stop within 100 cm for a station or unit and 64 cm for a pickup, then wait for explicit action. If already within that range, stop. Project a reachable approach point outside collision, not the object's blocked centre. There is no auto-channel, auto-loot, auto-attack or chase update. Loss of target observation cancels a pending target-approach order. A moving target requires a new click.

Static walkability may be used for blind ground orders, including an unexplored minimap region. This does not authorize a hidden dynamic target or expose unobserved actors, blockers or inventories. Route previews display only segments in explored terrain. Do not stream a full hidden-world debug path or navigation mesh to render a line. Normal actors retain the existing nonblocking crowd policy; hidden people must not alter avoidance or cursor result and become a path oracle.

Maintain derived walk/sprint caps, ramps, no jump, wall/tree collision, dash restrictions and native acceleration/braking. A two-second no-progress window triggers one static-route replan; after a further two seconds without progress stop with Path blocked. Proposed progress means at least 10 cm of along-path advancement. Do not oscillate or infinitely retry. Measure arrival and stop under delay, not just in the editor host.

## Facing and action commitment

Travelling normally faces path direction. Hovering or inspecting another unit never rotates the body. An explicit attack or targeted skill sets its intended aim for the committed action; its retained range, cone, rear, sight and life rules decide the result. Free camera motion does not alter aim. When idle without a committed action, preserve last facing. This makes facing usable for theft without coupling it to menu hover.

Basic attack is A then left-click a visible unit or terrain direction. It issues one retained v0.4 directional melee attack, not auto-chase or continuous attacks. The impact still chooses one nearest legal visible target in the retained cone and reach; selection alone is not a target lock. Skill target locks remain skill-specific. Attack direction is confirmed only on world click and never from hidden body coordinates.

Q/W/E/R arms a targeted skill unless it is a self channel. Step uses ground direction from own position, then its existing bounded distance, collision and 200 ms travel. Spark/Ambush use a visible legal body and retain their timing and target rechecks. No spell automatically walks into range. Pickpocket and environmental checks arm a target and begin only when the confirmation pointer is held. Patch begins when its assigned key or own hotbar button is held. Release before completion cancels held channels; accepted cooldowns remain spent. Use a visible reticle, legal-range ring and concise rejection.

A targeted press action confirms on left-button press. A targeted channel confirms on left-button press and requires holding that button to finish. A contextual F action or self/item shortcut channel requires holding that initiating key/button. These all route to the same Begin/UpdateHold/End action contract. Never require simultaneous F plus a skill key. Do not bind a complete channel to key-up or automatically cast a known but unequipped action.

Arming a target consumes no cooldown and does not stop an existing route. Accepted stationary channel begins by cancelling navigation and settling the body before its timer starts. No automatic approach or request retry occurs if out of range. Right-click while targeting cancels targeting and consumes that click; the next right-click can move. Right-click during a stationary channel cancels it and issues a new move. UI clicks only cancel a channel when explicitly opening a major panel; they never issue world movement.

For basic, Spark and Ambush windup/recovery, retain the existing legal locomotion and damage/cooldown rules. A move/stop command changes only locomotion and does not cancel or refund a committed combat action. If its native action temporarily forbids movement, retain at most the newest destination until movement becomes legal; S clears it. Step interrupts other actions only where its retained v0.4 rule permits. No general action queue is added.

## Camera and minimap

Retain the actual fixed yaw/pitch and zoom bounds, proposed 1100–1700 cm if unchanged. Free pan is bounded by authored world bounds, at a proposed 1800 cm/s. An 8 physical-pixel edge zone pans only over the unobstructed world while the game is foreground and no modal, drag or targeting state is active. Provide an edge-pan toggle, default on. Arrow-key pan works when no text or modal consumes it. Manual pan leaves follow mode; Space release keeps the current camera position and Home enables persistent follow.

Minimap left-click/drag pans the camera, right-click emits a ground move to that authored map location. Minimap clicks cannot cast, interact or select a hidden unit; a click on a visible marker remains a camera/move operation. Show the camera footprint and source legend. A distant camera position permits viewing authorized sources but never changes body-based action reach.

## Input ownership and interruption

Priority is focused text entry, modal/confirmation, drag capture, major panel, armed targeting, minimap, then world. Only one layer handles a gesture. Pointer-down ownership persists to release even if the pointer crosses a widget edge. Dragging an item out of the bag does not drop it into the world; cancel the drag. A world click that opens a window must not also activate an icon under the new window. Rebinding capture consumes the captured key.

Inventory/development/help/settings/trade are major nonpausing panels. Opening one sends Stop, releases all holds and cancels targeting/interruptible channels. It does not cancel a committed combat strike. While open, it blocks movement, attacks, sprint, casts, quick use and edge pan everywhere, including uncovered world regions. Inspect tooltips, scroll, Escape and explicit panel actions still work. Hover alone does not open a major panel. Closing restores world mode only after every held physical button has been released; never replay a held click.

Preparation Apply validates the draft locally, closes the development panel and arms a station transaction; it does not spend points. A fresh Hold F at the station performs the retained two-second confirmation with the expected build revision. Out-of-range/dead/protected/quiet-state failures reject normally; opening P away from a station remains useful for inspection but Apply is unavailable. Cancelling the armed transaction discards the draft without spending. This avoids a panel consuming the very F hold needed for confirmation. Inventory Use follows the same close-and-arm pattern; organization and explicit Buy/trade/drop-confirm buttons are panel transactions, not world-key fallthrough.

Focus loss, disconnect, death or terminal state cancels navigation, queued destinations, targeting, drag, holds and open trade. Focus regain is inert. Authority expires channel hold state using the existing bounded heartbeat; proposed maximum 300 ms without a fresh hold if the baseline lacks a value. Navigation also stops on explicit focus-loss/heartbeat expiry; proposed 500 ms heartbeat and two-second liveness expiry for a lost packet. Reconnect cannot revive an old-life route. UI never pauses the shared match.
