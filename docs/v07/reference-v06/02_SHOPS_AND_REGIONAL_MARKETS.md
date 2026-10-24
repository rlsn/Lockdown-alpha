# Shops and regional markets
## Stable official supply
The City Guild supplier visibly identifies itself as Official Supply, not a disguised contender. Its fixed unlimited catalog is Step/Spark/Patch/Parley books at 40 gold, Fiber at 4 and Ore at 6. Each purchase burns the paid gold and creates one item. There is no supplier wallet, buyback or automatic free refill into player bags. Books may still be bought for peers; known-book reading rules remain. No Crystal, Toolkit, Core or Seal is officially sold. Existing finite ward supplies are retained; no new ward vendor is required.

## Goods
| ID | Purpose | Regional market eligible |
| --- | --- | --- |
| Fiber | Raw input | Yes |
| Ore | Raw input | Yes |
| Crystal | Wilderness/special input | Yes |
| Toolkit | Intermediate crafted input | Yes |
| Core | Final procurement good | No; peer transfer allowed |

One unit is one unique carried instance. Inputs, Toolkit and Core drop on death and count toward the existing 24. No book/ward/Seal regional buyback. All eligible goods are fungible in definition but retain instance identity and lineage.

## Initial regional state
Each marked market has 150 gold in its institutional wallet, not in the trader's lootable personal purse. No later cash injections occur. Only the explicitly budgeted producer ticks may create market stock; no free refill/reset occurs.

| Market | Fiber stock/base | Ore stock/base | Crystal stock/base | Toolkit stock/base |
| --- | --- | --- | --- | --- |
| WestTown | 18 / 4 | 6 / 10 | 3 / 16 | 2 / 26 |
| EastTown | 6 / 8 | 18 / 6 | 3 / 16 | 2 / 26 |
| City | 8 / 6 | 8 / 8 | 8 / 12 | 2 / 24 |

Target stock T equals that commodity's initial stock at that market. Maximum stock is max(12, 2*T). S is current stock, B base price. Compute exact integer arithmetic:
```text
ask = clamp(ceil(B*(3*T-S)/(2*T)), ceil(B/2), ceil(3*B/2))
bid = min(floor(7*ask(S)/10), max(0, ask(S+1)-1))
```
ask(S+1) evaluates the same formula at one more unit of stock under the SAME event ratio. This price-impact guard ensures an immediate buy/resell loses at least one gold even near the scarcity clamp. A sell still requires room below the stock cap.

Bid and ask are per-unit quotes at current state before the transaction. Integer ceil divisions avoid floating disagreement. Do not round differently in UI and authority. Empty stock prevents buying; it does not prevent a funded market from buying from a player. Full stock prevents further player sales. Insufficient market cash refuses a sale without minting or negative balances. Public reserve/capacity and quote availability are deliberate institution facts.

## Transactions
A player Buy subtracts ask from the player, adds it to the market wallet and transfers one existing market instance to the bag. Sell transfers one owned unlocked eligible instance to market stock, subtracts bid from market cash and adds it to player cash. These are transfers, not gold/item creation or destruction.

Require alive/unprotected, body sight, 120 cm range, current market/commodity quote revision and exact price, owned item revision, capacity and no conflicting action/trade reservation. The existing major panel stops movement. One unit per explicit acknowledged action; pending disables duplicate clicks. Server changes invalidate stale quotes, which return Review updated price rather than silently charging changed terms. Market revision changes on any stock or cash commit; no stale sale against another player's earlier cash.

An intended cross-region or supply-to-market margin is legal: transport, finite cash, stock caps and consumption constrain it. A same-market immediate buy/resell cycle must lose gold for every configured legal stock state, including changed prices. Do not globally ban profit or cap player wealth arbitrarily. No restock timer may silently restore depleted cash/items.

## One scheduled shortage
At t=600 seconds, announce an EastTown Toolkit shortage; until t=780 multiply only that commodity's scarcity expression by 5/4 BEFORE the same fixed base-price clamps. The ask expression becomes ceil(B*(3*T-S)*5/(2*T*4)); bid uses the same next-stock guard and event ratio. At 780 revert, changing quote revisions at both boundaries. No surprise random outcome is required.

One global bonus order appears with the event: first valid delivery of one Toolkit to East before t=780 consumes it and mints 40 gold/20 XP, once, no endorsement. Three-second held completion must be strictly before expiry. No exclusive reservation of the order; competing accepted channels race through authority ordering and losers retain inputs. Publish exhausted/expired status. The price modifier lasts until 780 even if this order fills early. This event is not repeated and never refills cash or stocks.
