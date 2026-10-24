# B67 — v0.6 acceptance reconciliation

24 October 2026. Final scope is **development closeout with verification deferrals**,
per the user’s instruction to avoid lengthy tests, defer human review and move
to v0.7. This reconciles all 48 acceptance IDs; it does not mark all 48 passed.
Local engineering evidence retains the exact scope/fixtures in the linked milestone
reports. Partial means some required permutations or native/normal/human evidence
are missing. Deferred means the main release scenario was not run for v0.6.

No new Unreal run was made at B67: unchanged runtime hashes allow retention of
B66’s actual build/input/authority results. See [closeout](B67_CLOSEOUT.md) for
the named follow-ups and [ledger](FeatureLedger.csv) for original requirements.

| Closeout category | Rows |
| --- | --- |
| Deferred — not run | 3 |
| Local engineering evidence retained | 27 |
| Partial — scoped evidence retained | 17 |
| Source/document audit retained | 1 |

| ID | Requirement | Evidence retained | Closeout / deferred follow-up |
| --- | --- | --- | --- |
| E01 | Actual delivered v0.5 revision/toolchain/deviations and adopted economy defaults | [B60](B60_BASELINE_REVIEW.md) | Source/document audit retained |
| E02 | Normal roster/profile, clean reset and persistent institutional state survive service recreation/respawn | [B61](B61_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D04 |
| E03 | Clearly marked official noncontenders use proposed protection; ordinary bodies remain masked | [B64](B64_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D05 |
| E04 | Structured service panels work without new dialogue/chat or hidden-player role leakage | [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D05; V06-D07 |
| E05 | Five unique goods fit existing bag, drop correctly and reject quick-use; no stacking added | [B61](B61_IMPLEMENTATION.md), [B62](B62_IMPLEMENTATION.md), [B63](B63_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D07 |
| E06 | Stable supply creates one paid instance and burns exactly fixed cost; no buyback or new free supplies | [B62](B62_IMPLEMENTATION.md) | Local engineering evidence retained |
| E07 | Three initial market stock/cash/target/cap tables match adopted data | [B62](B62_IMPLEMENTATION.md), [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained |
| E08 | Integer ask/bid match every stock state and endpoints, including lower/upper clamps | [B62](B62_IMPLEMENTATION.md), [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained |
| E09 | Every legal same-market buy/resell cycle loses gold; intended cross-region margins are recorded | [B62](B62_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D03; V06-D05 |
| E10 | Regional Buy transfers existing item and payment; no mint/burn in aggregate | [B62](B62_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E11 | Sell transfers item/payment; empty market cash/full stock reject without loss | [B62](B62_IMPLEMENTATION.md), [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E12 | Stale quote, concurrent buyers/sellers, duplicate request and pending-click cases cannot charge altered terms or duplicate | [B62](B62_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D06 |
| E13 | Two finite outcrops award at most six total Crystal, no body/reconnect charge reset | [B63](B63_IMPLEMENTATION.md), [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E14 | Gathering range/sight/hold/capacity/race/interruption consume exactly one charge only on success | [B63](B63_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D06 |
| E15 | Toolkit consumes 2 Fiber/1 Ore/4 gold; Core consumes 2 Toolkits/1 Crystal/6 gold with exact parent lineage | [B63](B63_IMPLEMENTATION.md) | Local engineering evidence retained |
| E16 | Full-bag net-capacity craft succeeds if inputs make room; failed output creation consumes nothing | [B63](B63_IMPLEMENTATION.md) | Local engineering evidence retained |
| E17 | Craft interruption/death/late theft/reservation conflict releases safely and never shields wallet gold | [B63](B63_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D06 |
| E18 | Survey score 12/equipped extraction uses normal 1-second skill path, charge/capacity and cooldown | [B63](B63_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D07 |
| E19 | Analyze score 14/equipped coupon uses normal cast/CD; one local 30-second coupon, no stacking, two-gold discount and valid completion expiry | [B63](B63_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D07 |
| E20 | All classes can basic craft and earn Witness; no new free utility, universal price stat or changed combat formula | [B63](B63_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D03; V06-D05; V06-D08 |
| E21 | Eight consumption ticks remove real listed goods or skip empty; bounded producer receipts separately charge market cash and create goods | [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained |
| E22 | Consumer animation is tied to one receipt and does not create duplicate demand | [B64](B64_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D05; V06-D07 |
| E23 | Procurement consumes Core, awards 80 gold/60 XP and first-time Industrial endorsement, caps three per contender and 3*N global | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained |
| E24 | Purchased/peer/recovered owned Cores qualify; transferring a Core alone grants nothing | [B65](B65_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D06 |
| E25 | Endorsements bound/retained, paid budgets survive death/withdrawal and no excess Core is silently consumed | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E26 | Registered Charter purchase requires Witness/two distinct endorsements/100 gold, explicit wallet-bank split and one-purchase budget | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E27 | Old four-credit Seal path and new courier-credit awards are disabled in normal profile, including direct requests | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained |
| E28 | Public warning30/open60 permits every eligible contender to complete real ten-second Heart claim, not a wealth/last-survivor/private Seal win | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E29 | Sequential City Crystal asks 12/13/14 match formula; distinct Supply+Core path from three jobs leaves 15 gold after Charter before interference/coupons | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained |
| E30 | Three jobs+Supply+Core =225 XP; three visits =300, level four; no craft/trade/gather XP | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained |
| E31 | Initial gold endowment and all bounded mints/burns reconcile across every wallet after each batch | [B61](B61_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md); [receipt replay](evidence/B67/ReceiptReplay.txt) | Local engineering evidence retained |
| E32 | Per-definition item counts and lineage reconcile across bags/markets/drops/reservations/demand/crafting | [B61](B61_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md); [receipt replay](evidence/B67/ReceiptReplay.txt) | Local engineering evidence retained |
| E33 | Death tax is a burn, market/peer/theft are transfers, official purchases burn; no double accounting | [B61](B61_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md); [receipt replay](evidence/B67/ReceiptReplay.txt) | Local engineering evidence retained |
| E34 | Normal 25-minute run at one and four contenders explores scarcity/exhausted cash without negative stock/gold or silent resets | None for the deferred v0.6 scenario | Deferred — not run · V06-D03 |
| E35 | Competing market/craft/procurement/peer/theft/death requests serialize with due demand and final result safely | [B64](B64_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D06 |
| E36 | Native economy screens show pending/quote/capacity/hold/reject states and retained remapping/focus/layout correctly | [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D05; V06-D07 |
| E37 | Restricted clients see only authorized institutional facts and own private economy; distant price display remains timestamped | [B66](B66_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D01; V06-D07 |
| E38 | Actual cold build/cook/package and two remote clients work; measured delay preserves transaction commitments | None for the deferred v0.6 scenario | Deferred — not run · V06-D02 |
| E39 | Ten restarts, normal session, frame/transaction/query/memory checks against baseline show no persistent reservation/receipt leak | None for the deferred v0.6 scenario | Deferred — not run · V06-D04 |
| E40 | Real zero-kill production-to-contest demo, distinct economic approaches and human task evidence recorded honestly | [B65](B65_IMPLEMENTATION.md) | Partial — scoped evidence retained · V06-D03; V06-D05 |
| E41 | Physical bank deposit100 credits98/burns2; withdraw30 leaves68 and wallet+30; replay/stale/negative/overflow reject atomically | [B61](B61_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E42 | Theft/death affect carried wallet only, bank protected after commit; no remote access/interest; split Treasury payment validated | [B61](B61_IMPLEMENTATION.md), [B65](B65_IMPLEMENTATION.md), [B66](B66_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01; V06-D06 |
| E43 | STR-based weight and slot limits validate every pickup/purchase/craft/trade/preparation; no double-counted reservations or free mass | [B63](B63_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01; V06-D06 |
| E44 | Supply order consumes two Toolkits/50 gold reward/30 XP and grants only its distinct endorsement, bounded once per contender/N global | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E45 | Shortage event 600–780 reprices East Toolkit; one shared bonus order 40 gold/20 XP, completion before expiry, no endorsement/refill | [B64](B64_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01; V06-D06 |
| E46 | Charter death before use retains registration but delays until respawn+30; death after activation leaves opening active and spent | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
| E47 | Global window cannot overlap/extend; latest activation requires full window; endpoint tie/late sample gives no extra claim time | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01; V06-D06 |
| E48 | A nonpurchaser without Charter wins an opened contest under normal rules; old Seal/private bypass is unavailable outside windows | [B65](B65_IMPLEMENTATION.md) | Local engineering evidence retained · V06-D01 |
