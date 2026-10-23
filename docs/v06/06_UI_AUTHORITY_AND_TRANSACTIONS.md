# UI authority and transactions
## Extend the delivered shell
Keep v0.5 mapping, input capture, click navigation, own HUD/inspector and nonpausing major panels. Reuse vendor, inventory detail, quote, hold-progress and receipt widgets. Add Official Supply, Regional Market, Workshop and Procurement service variants. A selected market exposes institutional stock/reserve by explicit public policy, not the private inventory of arbitrary bodies. Clearly label official noncontender/protected services. Keep ordinary neutral bodies and private builds masked.

Market panel shows locality, item stock/cap, bid/ask, available cash and Buy/Sell reason. Workshop shows exact input instances, fee, output/net capacity, coupon/expiry and no free skill action. Procurement shows payout, remaining quota and own endorsement progress. Objective card explains Witness + two distinct endorsements + 100 gold, registered Charter and public opening, then a contested claim. No fake Sell buttons for books/Seal or inert dialogue choices.

Opening a service needs real body range/sight, not distant minimap sight. F arms/holds after panel closes; releasing/major-panel reopen cancels. The Apply/Use close-and-arm pattern remains. Transactions are pending until acknowledged, one explicit unit per buy/sell. Reject a stale quote with updated terms, never auto-accept a new price. New material icons show not quick-usable.

## Authority and identities
Keep pinned Unreal, existing movement/ability architecture and current engine assets. No new framework or mandatory GAS conversion. Use versioned data for goods, recipes, markets, budgets, institutional demand and service metadata. The JSON is a companion, not engine assets. Audit actual baselines before adopting proposed numbers.

Market stores actual item IDs, cash, per-good target/cap/base/stock and revision. Player build, endorsements, quotas/coupon, bank and wallet belong to persistent contender, not pawn. Nodes store remaining charges by stable node ID. All request/quote/session/life IDs retain owner validation. Do not accept client-computed prices, yields, coupon discounts or reward amounts.

Reservations for craft inputs and peer offers share one lock authority. An item cannot serve both. Market transactions serialize with peer trade, theft, gathering, crafting, procurement, death and objective creation. Existing outgoing-gameplay action cancels peer trade before executing. Incoming wallet theft does not shield offered gold and can invalidate affordability; final craft/procurement checks still apply.

## Commit order
Use native movement, due simultaneous combat, then death/reservation release/drops, then noncombat commits, then Heart/deadline. At a scheduled institutional tick, process its stock consumption in noncombat ordering before player market commits; stale player quotes reprice rather than paying different terms. Ties among player noncombat requests use existing role-neutral authority order, logged, not UI arrival animation.

Craft checks post-input capacity, ownership, fee and coupon expiry, consumes inputs/burns gold and creates output atomically. Procurement consumes output and awards bounded payment/XP and first-time Industrial endorsement once. Supply order and registered Charter checkout are separate atomic transactions. Validate everything before mutation and reserve/create real output identity; failed asset/item allocation cannot burn inputs or money. Log one causal receipt with per-wallet deltas and per-item transitions.

Each market stock mutation changes quotes/revision; every cash change changes global market revision. Consumers use real instances, deterministic instance choice (oldest creation receipt then ItemId), no random silent goods. Invalid purchase/sale cannot change availability or advance RNG. No economy randomness is required.

## Privacy and operations
Public institution facts may be replicated only as specified, without permanent human/NPC identity leakage for ordinary units. Own endorsements, margins/history, craft inputs and balances stay private. Market price discovery requires visiting/opening its service; last quoted prices may be shown with timestamp, never silently updated remotely. Procurement global remaining quota is an intentionally public institution fact; individual completion does not reveal private contenders.

Reset finite stock/cash once per match, all effects through receipts; service body recreation never resets the institutional record. Marked service immunity is a public rules exception, not a hidden human-detection endpoint. No new matchmaking/reconnect/production operations work.

Deliver native assets, bootstrap/build/cook/package and remote-client checks on the actual pinned environment. Reuse valid unaffected v0.5 evidence; do not invent compiled assets or human results from this document package.

## Banking weight and public-access override
Also implement [banking/cargo](11_BANKING_AND_CARGO.md). Total accounting includes bank balances; weight is final-state validated in every acquisition/transfer/build-change path. Official service UI can identify the institution without identifying ordinary bodies.
Implement only the distinct-endorsement/registered-Charter state machine. No normal physical Seal output or exclusive carried-item bypass.
Store Charter purchase, unused/spent flag, post-death readiness and global Warning/Open/Closed endpoints on match/contender state. Process scheduled window endpoints before Heart completion eligibility; completion exactly at close is allowed based on scheduled endpoint, after it is not. On terminal Heart result cancel all future economy/window commits.
Due institution ticks execute bounded production then consumption; at t=600/780 event changes apply before player quote commits. All scheduled work occurs once by scheduled tick ID, not render frame. No catch-up beyond match end.
