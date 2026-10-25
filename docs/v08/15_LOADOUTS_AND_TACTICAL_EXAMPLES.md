# Loadout prototypes and encounter traces

These are aspirational assembled builds, not free starting kits or proof a normal player can afford every component. Equip gates use natural stats; gear does not satisfy them. Every listed learned skill is one of the six active slots. All listed gear benefits still obey +6 total primary bonuses and secondary caps.

| Build | Natural investment | Shared six actions | Four accessories | Selected body equipment | Weakness/counter |
|---|---|---|---|---|---|
| River controller | INT11/WIS10 Arcanist start | Frostline, Water Jet, Gust, Counterpulse, Purge Touch, Farwatch Ward | Frost Prism, Cleansing Charm, Water Flask, Arcane Lens | Frost Staff, Arcane Focus, Scholar Hood, Channeler Vest, River Treads | High MP use; dry detour, spacing, guard, Mute against accessories; no sustained melee defense |
| Pursuit fighter | STR11/CON10 Vanguard start | Anchor Bash, Brace Wall, Hamstring, Grapple, Power Strike, Disarming Cut | Cleansing Charm, Command Banner, Purity Charm, Healing Salve | Iron Longsword, Guardian Shield, Iron Helm, Reinforced Leggings, Duelist Gloves | Long-range vision/Steam; item defenses can be muted; no ranged magical burst |
| Economic infiltrator | CHA11/INT10 Broker start; earn actual legacy Parley knowledge/gates | Legacy Parley, Legacy Analyze, Ledger Mark, Surety Shell, Smoke Veil, Slipstream after natural DEX10 | Broker Seal, Weighted Satchel, Recall Token, Seer Dust | Merchant Harness, Scout Cowl, Laborer Grip, Wayfarer Boots | Requires real DEX allocation and legacy score checks; scarce combat slots; Recall forbidden with objective cargo |
| Construct support | INT11/WIS10 Arcanist start; allocate CON9 for Rally Guard | Deploy Conduit, Sabotage, Farwatch Ward, Rally Guard, Rescue Line, Purge Touch | Command Banner, Cleansing Charm, Ward Totem, Gust Vial | Engineer Gloves, Field Toolkit, Channeler Vest, Scout Cowl | Aid consent is required, little solo damage; visible emitters can be destroyed/suppressed |
| Hybrid spellblade | Any eligible natural gates for selected learned skills | Frostline, Water Jet, Hamstring, Purge Touch, Farwatch Ward, Reflective Mail active | Frost Prism, Shadow Charm, Cleansing Charm, Arcane Lens | Iron Longsword, Reflective Mail, River Treads | Only five learned slots because gear reflection consumes slot6; Mute blocks reflection and accessory actives |

Economic example contains legacy adapter names rather than newly authored IDs. Implementation must resolve actual legacy definitions and requirements before the build is selectable. No free extra skill bar for work. Removing a consumed recovery item leaves an empty accessory slot until legal quiet rebinding. A passive accessory occupies its slot even without a button.

## River interrupt

A claimant begins a ten-second Heart claim while Wet. Defender releases Frostline at a legal visible line. The direct hit applies normal magical damage and attempts the Wet reaction. If unguarded, Stun lasts600 ms, claim resets, Wet is consumed and victim readiness becomes impact+6000 ms. A second attacker cannot repeat the reaction before readiness even if river refreshes Wet. They can still use an independently authored stun or explicit Interrupt; the specific Wet rule does not cap all CC.

If the claimant preapplied Purity Charm/Remedy Bind guard, that charge strips the reaction Stun. Damage may reduce HP, but a surviving claim continues. The Wet reaction lock still commits, creating a short counterplay window. Claimant cannot activate the charm after starting the claim without cancelling their own attempt; timing matters.

## Melt and sight

Frostline freezes river cells, applying slow. A later Firebolt impacts the frozen mask and transforms only its footprint to Wet+Steam. Steam blocks sight symmetrically across its connected union boundary. The defender's old target handle is insufficient for a new unit-target spell. Gust Vial clears its selected cells; server fog recomputation must authorize the body before target release. A public Heart attempt sensor does not itself authorize targeting the claimant.

## Economy versus preparation

A merchant carries a Recall Token in one accessory slot and passive capacity/fee accessories in two more. To equip a second offensive consumable they must give up another active/passive accessory and commit quiet preparation. Buying enough gear to fill all eleven bindings competes with books, market working capital and Charter cost. The retained v0.7 zero-kill Charter example has only15 gold surplus under its assumptions; this package does not claim it finances a full equipment build.

## Environmental tradeoffs

Water Flask sets up Frost or conductivity but also extinguishes flames, and makes the thrower vulnerable if they enter its cells. Frost Capsule slows an approach but cannot stop a stationary Heart claim. Shatter Burst trades persistent slow for one physical burst. Steam can protect an escape and hide a pursuing rival. None changes the navigable river graph; characters without these skills retain ordinary routes.
