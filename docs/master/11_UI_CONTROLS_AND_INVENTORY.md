# UI controls inventory and trade

## Interface direction

Use the information density and character-building access of a party RPG with the fast spatial awareness of a MOBA. A minimap, lower inspector, persistent own commands and readable world feedback form one system. Inspiration does not mean copying another game's assets or every convention.

The bottom inspector changes subject when another visible unit is selected. Self remains controllable. Own HP, active skills and quick items remain anchored and labelled Yours; inspecting a stranger cannot activate that stranger's skills or expose a hidden build. Exact private fields are Unknown unless a separate authoritative reveal rule permits them. Generic human/NPC bodies have the same inspection schema.

Show HP and resources that actually exist. An eventual mana system can add MP through resource metadata; drawing a fake or inexhaustible MP bar is not implementation. v0.5 defaults to the current no-mana rules unless the delivered baseline establishes otherwise and the change is documented.

## Commands and camera

Right-click terrain sets a navigation destination. Left-click selects. Attack, skill targeting, interaction and movement are distinct intents with visible cursors and cancellation. Avoid accidental violence from a neutral body's default click. Right-click approach does not automatically perform a channel, reveal hidden targets or chase a body after sight is lost.

Keep one controlled body and native collision. Ramps, trees, elevation, sprint and movement skill constraints still apply. A destination is not a teleport or permission to query hidden dynamic positions. Camera pan and minimap inspection never expand sight or interaction reach.

Layer input predictably: focused text, modal, item drag, targeted action, minimap and world have explicit ownership. A UI click never falls through into the world. Focus loss stops movement and held actions; returning does not replay them. Menus do not pause other people.

## Inventory and quick items

A large carried inventory and a smaller quick-item bar are distinct. An owned item has one identity and location. Quick slots are references to carried usable items, not extra storage, bonus skill slots or copies. Consuming, trading, dropping or losing ownership clears the reference. Sorting changes presentation, not identity.

Books teach knowledge through the real read action. Wards remain finite items. Historical Seals are unique objective items; the v0.6 economic profile uses a bound registered Charter rather than a transferable Seal. Learned actives still require the common skill slots. Attribute points, learned skills, gold and objective receipts are not ordinary movable inventory icons.

Exact capacities, stacking, encumbrance, equipment and death persistence are release rules. v0.6 adds a weight limit, physical bank and public Charter controls to the v0.5 UI. v0.5 proposes one item per 24 carried slots and four quick references, without equipment bonuses or stacks. Existing drop and knowledge-retention rules continue.

## Trade and access

Use one visual language for inventory, vendor purchase and consensual exchange. A vendor shows a public catalog and the owner's balance. Peer trade shows only voluntarily offered items and gold. Neither window reveals an opponent's private bag, class or full wallet.

Offers are versioned. Any change invalidates both approvals. Both sides explicitly accept the exact final offer before an atomic exchange; range, life, quiet state, balances, capacity and ownership are rechecked. Cancel on damage, leaving, disconnect or terminal match state without a partial transfer. A consistent refusal does not identify a quiet body as human or NPC.

No trade grants XP or avoids item death rules. Full markets, auction houses, negotiation simulation and equipment economy can follow after this interaction layer is validated.

## Readability and proof

Use consistent names, verbs, icon states, progress, rejection reasons and mapping-driven key labels. Do not depend on colour or sound alone. Test 1280×720, 1920×1080, high display scaling and a larger text setting. Reflow panels instead of shrinking essential controls beyond legibility.

Verify cold native builds and independent clients, payload privacy and atomic transactions, then observe uncoached people navigate, inspect, prepare, read, place, trade and claim. A static mockup demonstrates hierarchy; it does not establish a functioning game interface.

Revision 6 permits visibly official noncontender traders, distinct from neutral peer-trade bodies. Their market stock and buy/sell quotes are deliberate public service information; hidden personal wallets and ordinary inventories remain private. Add structured market, recipe and procurement panels using the existing nonpausing UI, without branching dialogue or a new chat system.
