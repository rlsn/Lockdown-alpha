# Full-profile economy, sources and progression

Only `v09_full` uses these overrides. `data/economy.json` is the full-profile commodity/contract config derived from the v0.6 contract inside v0.8. v0.8 combat/item/acquisition data remain inherited; there is no separately active legacy economy timer alongside it. Ignore older contest-by-proximity/damage wording in v0.6: v0.8 claim semantics govern.

Five regional market pools each start with600 gold, explicit stock/base-price rows and the inherited stock-dependent unit quote formula. Those are3,000 spendable gold, not five infinite vendors. The96 civilians each start with3 gold (288 total); travelers/guards/officials start with no spendable wallet. Contender money/starter kits require baseline audit; the package's selected incremental contender grant is0. Normal-profile initial accounted gold is3,288 under a zero-wallet baseline; if actual authorized starters differ, add that amount to the initial ledger and regenerate the bound before release.

Markets are Crownreach Treasury, Highfort Exchange, Lumen Garden Exchange, Riverport Harbor Exchange and Ironworks Foundry Exchange. Capital uses a treasury counter as its market rather than adding an unlisted31st city service POI. Every official supply endpoint shares global rare-stock limits per definition: visiting four shops does not create four Circle-III stock pools. Circle-I books40, Circle-II60, Circle-III90 with two copies per definition globally; Circle-IV remains assigned unique world sources. Catalog80 items/52 actives is inherited, not regenerated with renamed IDs.

World endowments are explicit sources, not extra reward gold:24 Water Flasks,24 Oil Flasks,24 Torches,12 Lanterns,12 Ropes,12 world Ladders;12 common caches together hold24 Fiber and12 Ore;64 saplings can each yield one Fiber by harvest; nine deposits have six one-unit charges each (five Crystal, two Ore, two Fiber). They do not replenish. Initial market stocks are also explicit endowments. No automatic commodity buyback for gear, books, lights, rope or ladders. Selling gathered commodities transfers a market's finite cash, never mints gold.

## Sources and sinks

| Gold source | Full-session maximum |
| --- | --- |
| Courier:20 ×25 per contender | 500 N |
| Core procurement:3 ×80 per contender | 240 N |
| Supply:1 ×50 per contender | 50 N |
| One global shortage order | 40 |
| Camps:72 ×12 | 864 |
| Towers:3 ×40 | 120 |
| Boss:1 ×90 | 90 |
| **Total reward mint** | **790 N +1,114 =20,074 at N=24** |

Initial gold3,288 +mint upper bound20,074 =23,362 before burns, under the stated starting-wallet assumption. This is an upper bound, not a forecast or entitlement. If mobs/jobs are unfinished, much less enters circulation. Shared global procurement limits remain3N Core orders andN Supply orders, with per-contender caps. No appearance of a new civilian, respawn, loot corpse, empty chest or simulation wake mints money.

Official purchases and recipes/quality fees burn; peer and market payments transfer; bank deposit fee ceil2% burns; death burns ceil15% carried wallet; Charter fee100 burns. Producers spend from real market reserves into an abstract nonspendable sink. At t=180,360,…,5,220 s, Riverport may create one Fiber for2 gold and Ironworks one Ore for3, then consume one of their own commodity and one Capital Toolkit.29 scheduled opportunities give at most145 producer burn. Skip if insufficient cash/stock/capacity, no catch-up or hidden deficit funding. Consumption destroys actual items and pays no gold. The one Toolkit shortage window is1,800–1,980 s, global bonus order once, not once per city.

`goldEnd = goldStart + rewardMints − burns`, summed across wallets, banks and all five market reserves. For each definition: `itemEnd = itemStart + created − consumed − explicitCleanup`, summed across carried/equipped, world, containers, markets and installed rope. A carried-world prop and installed item occupy one location, not two. State changes preserve instance ID; rare drops/activation/crafting retain lineage. End-match cleanup is separate from in-match economic sinks.

## Work and profession path

Courier boards issue bounded contracts with distinct reachable endpoints250–800 m apart by strategic route distance,25 gold/45 XP,20 paid jobs per contender. The server reserves a unique sealed contract token; it is not ordinary sellable cargo and cannot be duplicated, discarded for gold or counted as a book. One active courier job per contender. Death drops the token; the same owner may recover and finish, or abandon with no payout/no replacement of consumed paid quota. Quota is consumed on successful delivery, not on acceptance; abandoned contracts use a separate bounded issuance counter of40 per contender to prevent spam. Rivals can move/drop the token but cannot redeem another owner's contract. New job issuance needs no hidden opponent facts.

Industrial and Supply endorsements remain distinct real inputs:one Core→80 gold/60 XP, max3;two Toolkits→50 gold/30 XP, once. All city work counters share these budgets. Basic Toolkit/Core recipes and6 s quiet channels remain, with no passive trade/craft XP. Specialist skill shortcuts require learned/equipped slots and actual gates. Bank counters share one per-contender account across five cities; physical range/quiet/deposit fee rules remain. Only Crownreach Treasury sells the registered Charter.

Witness remains the existing audited Heave/Parley/profession route from the baseline, with no free world-interaction substitution. The supplied inherited docs describe alternatives but do not fully specify the old score/cooldown implementation. B92 must locate and record it in the actual repository; if absent, implement an explicit agreed contract rather than silently grant Witness. This is a flagged baseline dependency, not authorization to skip the zero-kill route test.

Full-profile first settlement visits grant25 XP once per distinct settlement ID, up to11=275 per contender; ruins grant50 discovery XP each once, never per life. Camps72×90, towers3×150 and boss250 remain finite world combat XP; PvP has the inherited per-contender cap. No XP from repeated surface reactions, guard/wildlife kills, harvesting, cargo pickup, market turnover or ladder use. Reaching level15 is not guaranteed to everyone; record actual distributions. The map supports growth through work and combat without requiring all civilians to fight.

The inherited zero-kill Charter arithmetic (three couriers75 −Toolkit inputs36 +Supply50 −Core inputs54 +procurement80 −Charter100 =15) remains an illustrative price-state check. Actual full-profile quotes, Witness costs, travel, purchased defenses and competition must be measured. Do not claim the15 spare gold funds a full combat build. Additional courier jobs are legal options, not free money on match start.

