# Unreal authority and data contracts

## Extend the current implementation

Keep the pinned Unreal version, native movement and observation boundary. Inspect actual v0.3 code first. If it already uses GAS, extend that implementation. If it uses focused C++ components, retain one coherent progression and activation path. A mandatory GAS conversion or a second parallel damage system is not required.

Use stable IDs and validated data definitions for class starts, skills, rank costs, subclasses, books, tasks, region nodes and profile tuning. Unreal Data Assets or Primary Data Assets can support this registry. Generate and save real assets through Unreal; do not hand-author fake binary packages. [Epic Data Assets](https://dev.epicgames.com/documentation/unreal-engine/data-assets-in-unreal-engine).

The JSON companion is source tuning, not an Unreal asset or a compiled implementation. Import it deterministically or generate equivalent native assets, recording schema version and checksum. Validate IDs, references, ranges, class totals, positive costs, skill formulas and available cue assets before play.

## Authoritative state

| Domain | Required state |
| --- | --- |
| Character build | Class, six base attributes, allocated points, character XP and level, unspent currencies, subclass, build revision |
| Knowledge | Learned skill IDs and ranks, separate from four equipped slot IDs |
| Availability | Absolute skill cooldowns, current action, last positive-damage combat time and protection |
| Economy | Gold, unread book instances, Seal ownership, work credits, paid-contract and craft counters |
| World receipts | Charter Witness, region visits, sentinel reward count and persistent wallet state |
| RNG | Private Theft stream and committed attempt receipt, never a client-controlled roll |
| Identity | Match, contender, life, wallet and item IDs with correct lifetime and exposure |

Build state survives pawn replacement. A temporary body never becomes the only owner of learned skills, cooldowns or paid budgets. A respawn cannot reset target retry windows, rank, item ownership or NPC gold. A match restart resets all of them deliberately.

## Activation boundary

All active entry points resolve from the owner's slot and build revision. The controller cannot activate an arbitrary learned but unequipped SkillId. Hotkeys, context menus, old Q or Space, bots and test helpers use the same legality path. Basic actions have explicit distinct IDs and cannot impersonate Parley, Analyze or another learned skill.

Validate connection ownership, match and life generation, request ID, slot, rank, current state, cooldown, target authorization, range and geometry before commitment. Snapshot source attributes, rank and subclass at acceptance. Recheck target life, position, sight and current target resistance at impact or commit. Do not trust a client-supplied score, damage, rank, price or roll.

Retain native movement prediction. Derived speed limits update through the current authoritative movement component and its prediction contract. Step distance changes only through that component's compatible dash implementation. Do not add another per-frame position integrator for attribute scaling.

## Ordering

Use the v0.3 authority sample after native movement. Process due combat as a simultaneous batch, finalize death and drops, then noncombat completions, then the Heart result and deadline. Damage on a completion batch prevents book reading, preparation, witness, delivery, sabotage, fabrication or claim completion. A dead source cannot finish a queued transaction.

For a prepared build commit, revalidate the entire plan and costs against the expected revision, deduct once and apply atomically. Apply derived-stat changes without free healing. A valid multi-rank purchase must pay every intermediate rank cost. No other field changes on a rejected plan.

For theft, revalidate target state, consume one random result and transfer the bounded existing balance in one commit. Concurrent attempts serialize against current purse balance; they cannot spend the same gold twice. A reused attempt ID returns its receipt and never advances the RNG stream.

For simultaneous item pickup or sabotage commits use a seeded role-neutral arbitration order. Only one actor acquires a particular item or changes the globally locked tower state. Already accepted cooldowns remain consumed for a losing completion. No result callback can overwrite a terminal Heart receipt.

## Transactions

| Receipt | Atomic effect |
| --- | --- |
| Level threshold | Grant each level's points once |
| Preparation | Spend all planned points and apply legal attributes, ranks, subclass and slots together |
| Book purchase | Deduct 40 and create one owned instance, or change nothing |
| Book reading | Consume an owned instance and teach rank one once |
| Theft | One outcome and bounded transfer; no repeat draw |
| Witness | Grant once per contender through one valid equipped check |
| Work and exploration | Pay only a fresh permitted accomplishment |
| Sentinel reward | Credit a living finisher within that contender's two-reward budget |
| Fabrication | Consume four credits and 100 gold and create one Seal, or change nothing |
| Death | Apply loss, retain build, drop physical valuables and schedule one return |
| Heart | One winner through one valid completed acquisition |

Use integer currency and point arithmetic. Use a consistent numeric representation for fractional HP and damage and clamp outcomes. Do not round the derived 21.5 damage into a different rule in only the UI or one network path.

## Privacy and observation

Exact attributes, class and subclass, library, ranks, loadout, points, cooldowns, wallet balance and task state are owner-private unless an explicit public rule authorizes a specific cue. Seeing a cast can reveal its SkillId through a readable effect. It does not reveal rank, the remaining hotbar or the full character sheet.

Keep role-neutral remote proxies and opaque IDs. Class must not choose a unique mandatory model, network actor subclass or material. Players may infer information from actual behavior. Do not transmit the six attributes to all clients merely because an example AttributeSet replicates them. Audit the selected implementation's replication settings. [Epic attribute replication](https://dev.epicgames.com/documentation/unreal-engine/gameplay-attributes-and-attribute-sets-for-the-gameplay-ability-system-in-unreal-engine).

Geometric visibility still authorizes targets first. WIS does not expand body sight or pierce blockers. Survey exposes only the authorized clue or task result. Theft previews omit exact private DC and balances. Victim warnings and cast cues cannot leak hidden attacker coordinates through audio, particles, object pointers or diagnostics.

Use the same build and activation components for the trained NPC. Ordinary residents can have simple policies and wallet data without full competitive progression. A native AI shortcut cannot heal, roll or award itself outside those components.

## Asset and UI requirements

Create class selection, character development, learned-library and four-slot hotbar panels, book inventory, task preview, receipt feedback and profile information. Menus do not pause multiplayer. Server acknowledgements drive committed state; previews never grant benefits.

Show available versus unlearned versus learned-unequipped versus cooling-down distinctly. Explain why a utility action is unavailable and show its slot requirement. Empty slots and unspent points are valid. Preserve native focus and key release when panels close or the window loses focus.

Use region data instead of magic district indices. World-graph validation checks IDs, connected paths, settlement service references, witness alternatives and first-visit reward uniqueness. Two maps with different node counts must load the same rules in tests, even though only one authored region is required for play.

## Verification and operation

Keep cold build, asset bootstrap, cooking and two independent remote-client checks from v0.3. Record which were actually available. The presence of this package does not demonstrate compiled assets or working Unreal code.

Log rule/config version, match, request and server receipt IDs for point spend, learning, random result, transfer and acquisition. Private seeds, target attributes and wallets remain server diagnostics, not ordinary player logs. Unreal random streams can support reproducible sequences, but project seed privacy and one-draw commitment require additional design. [Epic random streams](https://dev.epicgames.com/documentation/en-us/unreal-engine/random-streams-in-unreal-engine).

Deliver source and real assets, a completed baseline ledger, synchronized tuning, tests, logs, known limits and demonstrations using normal rules. Separate test-only grants and the 90-minute soak from actual progression and human strategy evidence.
