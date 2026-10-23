# Unreal contracts and authority

## Native integration

Retain the actual pinned Unreal version, current ability framework, filtered remote views and native CharacterMovement. Use existing Enhanced Input and UMG infrastructure. CommonUI is optional only if already appropriate; do not require a UI-framework migration. Create real saved assets through the engine and keep deterministic bootstrap/cook scripts.

Navigation is a foundation spike before UI expansion. Use native navigation queries and a path-follow adapter feeding the existing movement component. Keep one possession/authority path; do not swap the player to an AI controller, teleport along points, call a second position integrator or assume a single-player SimpleMove example solves remote prediction. Establish either the existing predicted owner-steering integration with validated server orders or a fully tested native server-follow integration. Record which actually works on the pinned engine, including prediction/corrections, then use it consistently. A remote client must stop, supersede orders, sprint, dash, handle a ramp and respawn before this gate passes.

Navigation intent includes match, life, owner, order sequence/epoch, destination or currently observed approach handle and command kind. Authority validates bounds, rate and static walkability; a hidden target pointer is never legal. Proposed input rate cap is ten destination requests per second with newest-order coalescing. Exceeding the cap must not queue old moves. S/focus/death cancellations take precedence over coalesced moves. Maintain existing native correction and speed enforcement.

## UI state and read models

| Read model | Allowed content |
| --- | --- |
| OwnCharacter | Own HP, build, resource entries, cooldowns, points and life |
| OwnInventory | Owned instance/slot/revision, bindings and balance |
| ObservedUnit | Neutral observed handle, authorized silhouette/cues and explicit known/unknown fields |
| WorldMap | Static authored map, explored/visible cells, authorized sources and filtered dynamic markers |
| VendorQuote | Public catalog/version, price and own eligibility |
| TradeView | Temporary session/offer handles, voluntarily offered terms, consent state and own validity |
| ActionFeedback | Own request/receipt, permitted reason, pending/committed status and progress |

Widgets never reach through an arbitrary pawn or PlayerState to fill a tooltip. Data is authorized before serialization, not hidden after widget binding. Reuse the filtered observation boundary for selection/hit testing, minimap, outlines, cursor changes, sounds and effects. Selected unit is a weak observed reference invalidated by view revision/life; no retained private identity enables reacquisition.

Separate ControlledSubject, InspectedSubject, ArmedAction, PointerCapture, CameraMode, MajorPanel and ActiveTrade. Selection is not possession or command context. Skills always resolve from own equipped slots; item shortcuts always resolve own carried ItemId. Disabled widgets do not replace authority checks.

Use change events/versioned snapshots rather than per-frame polling the entire world or reconstructing all 24 item widgets. Keep native view and UI work measurable. Major screens can cache static metadata but discard unauthorized dynamic payloads immediately when view/life changes.

## Commands and receipts

| Command family | Commit requirement |
| --- | --- |
| Move/stop | Owner/life/order validation, native collision and cancellation epoch |
| Skill/basic action | Retained slot, rank, geometry, timing, aim, life and cooldown checks |
| Inventory organization | Own inventory/item revision; no duplicate binding or ownership transfer |
| Read/place/drop | Actual owned item, legal state, target and one atomic consume/drop outcome |
| Buy | Current quote, price, balance, capacity and exactly one creation |
| Trade edit/Ready/Confirm | Current session, revision/digest, consent, reservation and final limits |

Keep the existing request deduplication and match/life rejection. New inventory revisions increment on committed changes, not pending ghosts. A layout sort should not invalidate unrelated skill build revision. Reserving an offered item increments its relevant item/container state and resets consent. Reject stale data without partial mutation.

Skill casts and holds retain the existing server timing. The router produces explicit Begin/UpdateHold/End/Cancel rather than invoking effects from UI events. Stale hold expiry remains authoritative. New navigation liveness is separate from deliberate continued path following while the client is healthy.

## Transaction order

After native movement, process due combat together, finalize death and reservation release/drop, then noncombat transactions, then terminal Heart/deadline logic under retained ordering. A terminal Heart result invalidates any remaining session and prevents later commits. Tie-break noncombat transactions through the existing role-neutral server order and receipt IDs, not UI frame timing.

Do not escrow/protect gold in peer trade. Wallet changes invalidate approvals. At final exchange, project both post-transfer bags and balances before mutating either; recheck every reserved item. Apply one result across both inventories, gold and quick references. On failure apply none. Death and confirmed trade cannot each claim the same instance. Physical reservations never suppress mandatory death handling.

An incoming theft during trade can transfer the normal amount from the current wallet, then invalidate the trade approval. A failed theft exposure has its existing meaning; do not fabricate damage solely to cancel a trade. Range/sight/protection and inactivity may independently cancel it. No reservation shields gold or creates XP.

## Data compatibility and observability

Version input/layout and inventory/trade data separately from unchanged combat tuning. The JSON companion lists proposed UI changes and inherited defaults; it is not a replacement for the actual v0.4 game config. Record actual baseline deviations and synchronize prose, JSON and real assets. Do not hand-edit binary assets or silently alter skill formulas.

Log server order, request, receipt, view and trade revision information for diagnostics. Ordinary players see clear outcomes, never production RNG seeds, hidden target coordinates, internal human/NPC role, wallet diagnostics or opponent inventories. Use a server log plus restricted client capture for privacy verification.

Store local keybindings, edge-pan, UI text scale and camera preference separately from authoritative match state. Reset-to-default is local UI behavior; it cannot reset cooldowns, purses, class, item ownership or trade receipts. Match restart clears all sessions, reservations, targeting, orders, quick references and dropped match items. New life retains appropriate build but requires new inputs.

## Official references

Reviewed 21 October 2026. Use documentation matching the installed engine; no upgrade is mandated by these links.

- [Epic Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) describes action/context separation and priority.
- [Epic Navigation System](https://dev.epicgames.com/documentation/unreal-engine/navigation-system-in-unreal-engine?lang=en-US) describes navmesh-based routing; project networking/cancellation still require implementation and evidence.
- [Epic DPI Scaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine) provides native scale rules.
- [Epic UMG Safe Zones](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-safe-zones-in-unreal-engine) supports safe-area layout testing.

These engine facilities do not automatically establish hidden-information safety, atomic trade or correct remote movement. Validate the actual project behavior.
