# Sources, assembly and specialized crafting

Hybrid acquisition is release scope. Buying, assembling, crafting, finding and peer trading are alternative routes where supply permits; no profession is mandatory to assemble ordinary equipment. Standard recipes are public from match start, deterministic, and normally use two or three component definitions. Assembly is a short physical workshop transaction; portable kits and disassembly are deferred.

| Output | Exact inputs | Fee burned | Station | Held time |
| --- | --- | --- | --- | --- |
| harpoon_halberd | 1 iron_halberd, 1 grappling_chain | 8 | town_workshop | 4.0 s |
| conduit_staff | 1 simple_staff, 1 Crystal, 1 Toolkit | 8 | town_workshop | 4.0 s |
| null_staff | 1 simple_staff, 1 null_bell, 1 Toolkit | 8 | town_workshop | 4.0 s |
| rift_staff | 1 simple_staff, 1 grappling_chain, 1 arcane_catalyst | 10 | advanced_forge | 6.0 s |
| oath_mace | 1 iron_halberd, 1 Toolkit, 1 Core | 10 | advanced_forge | 6.0 s |
| null_dagger | 1 watchblade, 1 counterglyph, 1 Core | 10 | advanced_forge | 6.0 s |
| reflective_mail | 1 bastion_plate, 1 mirror_sigil, 1 Core | 10 | advanced_forge | 6.0 s |
| anchor_greaves | 1 pathfinder_boots, 1 Ore, 1 Core | 10 | advanced_forge | 6.0 s |
| scholars_sigil | 1 runic_staff, 1 Core, 1 arcane_catalyst | 10 | advanced_forge | 6.0 s |
| iron_oath | 1 bastion_plate, 1 Core | 10 | advanced_forge | 6.0 s |
| blink_charm | 1 grappling_chain, 1 Core, 1 arcane_catalyst | 10 | advanced_forge | 6.0 s |
| heartbreaker | 2 Core, 1 arcane_catalyst, 1 heart_fragment | 20 | heart_forge | 8.0 s |

## Supply routing

Official Supply adds every priced equipment item in the catalog plus all priced counter-consumables, unlimited at fixed prices. No buyback and no resale into the commodity markets. Paid purchases burn gold and create exactly one item. Reliable counter supply therefore cannot be cornered by purchasing a finite shared stock. Travel, cost, three quick slots and combat lock still constrain use. Official services remain nonwinning, noncombat, nonlootable bodies; no citywide immunity follows.

Circle-I books are 40 gold at City; Circle-II books 60 at West/East trainers, unlimited. Circle-III books are 90 at East Archive, two physical paid creations per ability globally, decremented only on commit. Circle-IV books are one fixed copy each: Temporal Lock/RuinA, Grand Exchange/RuinB, Gate Weave/RuinC, Sovereign Ward/boss. Rare copies are transferable until read and drop as cargo. Archives do not refresh on actor recreation. Legacy Step/Spark/Patch/Parley 40-gold catalogs remain and are not silently aliased to different new spells.

Ruins each contain one unique key, one catalyst and one named capstone book. Their identities are endowed once at reset in closed cache ownership, counted in the item ledger. Opening a cache is a four-second quiet/sight/range hold; exposes a loot panel, does not automatically transfer contents. Pickups validate capacity and ownership individually. Key pickup uses a three-second hold. Others use normal explicit pickup. No per-player loot copy. Guardian boss creates one Heart Fragment and one Sovereign Ward book on its one paid defeat receipt. Last valid damage source receives gold/XP; physical drops remain contestable and are not auto-bound. Three catalysts suffice for one Heartbreaker and two ordinary upgrades; no guarantee each player can obtain one.

For completed gear purchases, East's equipment work-order panel offers a Buy completed action for each non-T4 recipe output. It buys only missing immediate inputs officially sold at displayed fixed prices, requires the buyer to supply all nonofficial immediate inputs (including Toolkit, Core, Crystal and catalysts), executes the selected recipe and burns missing official component prices plus that recipe fee. It never recursively fabricates commodity intermediates or bypasses their normal stations/holds. Purchased subcomponents are transient lineage entries consumed within the atomic transaction, not extra bag slots. Final bag count/weight is checked. Work order has the same hold/station requirements as assembly and never bypasses a scarce catalyst or recipe. No infinite catalog for Heartbreaker or Tower Relic. Players can also sell finished gear through consent-based peer trade; commodity market resale scope remains unchanged.

## Transaction and quality rules

Reserve exact owned inputs with existing item locks. Require alive, unprotected, quiet six seconds, body sight and 120 cm. Moving, positive damage, mute where the operation is item activation, death, focus loss or stale hold cancels without consumption. Assembly itself is an institutional interaction rather than a spell/item cast, so silence/mute alone do not prevent it; hard disable still cancels. No equipped input is eligible: explicitly unequip first. Gold is never protected escrow. Validate projected output weight/count after input removal, fee, recipe/station revisions and inventory revisions at commit. Stage output identity before mutation. Any failure transfers/consumes nothing.

Output tier is authored, not inferred from price. Combining loses consumed component craftsmanship: output is Standard and preview says so. Optional quality upgrades preserve the same item ID, tier, active and cooldown endpoints: Standard→Fine consumes one Toolkit and 6 gold at workshop/4 seconds; Fine→Masterwork consumes one Toolkit+one Crystal and 12 gold at advanced forge/6 seconds. No randomness, degradation, reverse conversion or XP. Reject quality upgrades on items with no eligible numeric field rather than charging for no benefit. Broker's Seal reduces these equipment/quality fees by one, minimum one; it does not discount v0.6 commodities, Core/Toolkit recipes, books, bank fee or Charter. Analyze's v0.6 coupon applies only to the original two commodity recipes, not these new equipment fees.

Heartbreaker consumes two Cores, one catalyst, one boss Fragment and 20 gold at HeartForge over eight seconds. It is a physical always-drop objective object, never ordinary retained gear. No character skill gate, so merchants may purchase the inputs and fighters may sell them. It is used for the artifact access route through the quiet physical antechamber F interaction, without a quick-item slot, not a combat DPS upgrade. Objective activation is an explicitly authored world interaction, never permission to activate ordinary unslotted equipment. Skill training never combines duplicate books: one book teaches Rank 1 once; ranks spend mastery points. Variants are a future knowledge feature, with one-per-family recommendation recorded, not dummy usable content in v0.7.
