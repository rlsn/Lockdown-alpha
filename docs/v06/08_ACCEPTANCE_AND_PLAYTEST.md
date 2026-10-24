# Acceptance and playtest
These are the original planned checks, not their results. Final status is in
[B67_ACCEPTANCE](B67_ACCEPTANCE.md) and [B67 closeout](B67_CLOSEOUT.md): the user
closed v0.6 on 24 October 2026 with minimal verification and deferred human review/
extended technical tests. The requirements below remain the historical contract;
unrun tests are not passes. Use pure arithmetic for model checks, real authority for transactions, native clients for input/privacy, and humans for strategic choices. Preserve unaffected valid v0.5 evidence.

| ID | Required evidence |
| --- | --- |
| E01 | Actual delivered v0.5 revision/toolchain/deviations and adopted economy defaults |
| E02 | Normal roster/profile, clean reset and persistent institutional state survive service recreation/respawn |
| E03 | Clearly marked official noncontenders use proposed protection; ordinary bodies remain masked |
| E04 | Structured service panels work without new dialogue/chat or hidden-player role leakage |
| E05 | Five unique goods fit existing bag, drop correctly and reject quick-use; no stacking added |
| E06 | Stable supply creates one paid instance and burns exactly fixed cost; no buyback or new free supplies |
| E07 | Three initial market stock/cash/target/cap tables match adopted data |
| E08 | Integer ask/bid match every stock state and endpoints, including lower/upper clamps |
| E09 | Every legal same-market buy/resell cycle loses gold; intended cross-region margins are recorded |
| E10 | Regional Buy transfers existing item and payment; no mint/burn in aggregate |
| E11 | Sell transfers item/payment; empty market cash/full stock reject without loss |
| E12 | Stale quote, concurrent buyers/sellers, duplicate request and pending-click cases cannot charge altered terms or duplicate |
| E13 | Two finite outcrops award at most six total Crystal, no body/reconnect charge reset |
| E14 | Gathering range/sight/hold/capacity/race/interruption consume exactly one charge only on success |
| E15 | Toolkit consumes 2 Fiber/1 Ore/4 gold; Core consumes 2 Toolkits/1 Crystal/6 gold with exact parent lineage |
| E16 | Full-bag net-capacity craft succeeds if inputs make room; failed output creation consumes nothing |
| E17 | Craft interruption/death/late theft/reservation conflict releases safely and never shields wallet gold |
| E18 | Survey score 12/equipped extraction uses normal 1-second skill path, charge/capacity and cooldown |
| E19 | Analyze score 14/equipped coupon uses normal cast/CD; one local 30-second coupon, no stacking, two-gold discount and valid completion expiry |
| E20 | All classes can basic craft and earn Witness; no new free utility, universal price stat or changed combat formula |
| E21 | Eight consumption ticks remove real listed goods or skip empty; bounded producer receipts separately charge market cash and create goods |
| E22 | Consumer animation is tied to one receipt and does not create duplicate demand |
| E23 | Procurement consumes Core, awards 80 gold/60 XP and first-time Industrial endorsement, caps three per contender and 3*N global |
| E24 | Purchased/peer/recovered owned Cores qualify; transferring a Core alone grants nothing |
| E25 | Endorsements bound/retained, paid budgets survive death/withdrawal and no excess Core is silently consumed |
| E26 | Registered Charter purchase requires Witness/two distinct endorsements/100 gold, explicit wallet-bank split and one-purchase budget |
| E27 | Old four-credit Seal path and new courier-credit awards are disabled in normal profile, including direct requests |
| E28 | Public warning30/open60 permits every eligible contender to complete real ten-second Heart claim, not a wealth/last-survivor/private Seal win |
| E29 | Sequential City Crystal asks 12/13/14 match formula; distinct Supply+Core path from three jobs leaves 15 gold after Charter before interference/coupons |
| E30 | Three jobs+Supply+Core =225 XP; three visits =300, level four; no craft/trade/gather XP |
| E31 | Initial gold endowment and all bounded mints/burns reconcile across every wallet after each batch |
| E32 | Per-definition item counts and lineage reconcile across bags/markets/drops/reservations/demand/crafting |
| E33 | Death tax is a burn, market/peer/theft are transfers, official purchases burn; no double accounting |
| E34 | Normal 25-minute run at one and four contenders explores scarcity/exhausted cash without negative stock/gold or silent resets |
| E35 | Competing market/craft/procurement/peer/theft/death requests serialize with due demand and final result safely |
| E36 | Native economy screens show pending/quote/capacity/hold/reject states and retained remapping/focus/layout correctly |
| E37 | Restricted clients see only authorized institutional facts and own private economy; distant price display remains timestamped |
| E38 | Actual cold build/cook/package and two remote clients work; measured delay preserves transaction commitments |
| E39 | Ten restarts, normal session, frame/transaction/query/memory checks against baseline show no persistent reservation/receipt leak |
| E40 | Real zero-kill production-to-contest demo, distinct economic approaches and human task evidence recorded honestly |
| E41 | Physical bank deposit100 credits98/burns2; withdraw30 leaves68 and wallet+30; replay/stale/negative/overflow reject atomically |
| E42 | Theft/death affect carried wallet only, bank protected after commit; no remote access/interest; split Treasury payment validated |
| E43 | STR-based weight and slot limits validate every pickup/purchase/craft/trade/preparation; no double-counted reservations or free mass |
| E44 | Supply order consumes two Toolkits/50 gold reward/30 XP and grants only its distinct endorsement, bounded once per contender/N global |
| E45 | Shortage event 600–780 reprices East Toolkit; one shared bonus order 40 gold/20 XP, completion before expiry, no endorsement/refill |
| E46 | Charter death before use retains registration but delays until respawn+30; death after activation leaves opening active and spent |
| E47 | Global window cannot overlap/extend; latest activation requires full window; endpoint tie/late sample gives no extra claim time |
| E48 | A nonpurchaser without Charter wins an opened contest under normal rules; old Seal/private bypass is unavailable outside windows |


## Model versus game evidence
Enumerate stock states through each cap and assert prices/spread, finite market balances, capacity and projected transactions. Model three sequential Core cycles with actual Crystal stock updates, not constant prices. Add optional coupon and two simultaneous buyers. These are specification model checks, not implementation passes.

In authority tests permute quote change, demand tick, sale, trade reservation, input consumption, theft, death and item pickup on the same batch. Test negative/overflow gold, unauthorized instance IDs, expired coupons and old life/quote requests. A private wallet offered in peer trade remains stealable. A marked institution is the only public-service exception.

## Native and human review
Use actual packaged builds and at least two independently controlled native clients at measured 100/150 ms RTT, up to 25 ms one-way jitter. Retain established frame/view budgets and record economy transaction/UI cost separately. Complete one normal 25-minute session at one and at four contenders where available; lack of participants remains an explicit unrun case, not a bot/human substitution. Ten restart checks target new market, coupon, node and receipt lifetimes. Do not repeat a 90-minute soak without a new lifetime concern; technical soak is not long-session pacing evidence.

Observe at least two human economic approaches: supply-and-craft/procurement and gathering/hauling/trade, with another participant contesting resources/claim where available. Record raw decisions, prices, net margin, travel, inventory occupancy, failed quotes, skill-slot tradeoffs, shortages, acquisition times and intervention. Ask whether players understood official versus peer services, bid versus ask, real profit after fees, and why wealth does not win automatically. Use normal earnings for the complete route; fixtures only isolate boundary cases.

No promise of equilibrium, equal class win rate or full 60–90 minute enjoyment from small samples. If tools/humans are unavailable, finish feasible work and label precise outstanding checks. No invented participants or gameplay results.
