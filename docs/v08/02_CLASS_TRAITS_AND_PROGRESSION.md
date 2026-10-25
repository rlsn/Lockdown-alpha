# Class traits and teaching presets

Exactly one intrinsic trait is the only exception to item-only persistent passives. Immutable per match, no trait button, no slot consumption. Traits are not Breakable in v0.8 and grant no universal immunity. Signature skills consume one of six slots, are replaceable, and remain cross-class learnable. No separate class active slot. Ordinary skills use natural gates, never exclusive class locks.

| Class ID | Trait | Exact rule | Signature suggestion |
|---|---|---|---|
| vanguard | Directional Discipline | Own Brace Wall frontal arc is140 degrees instead of120; no damage/guard/proc bonus. | brace_wall |
| infiltrator | Patient Pursuit | Own Ledger Mark clue lifetime is5000 ms instead of4000; still only creates samples in authorized visible cells. | veil_step |
| arcanist | Field Practice | Own newly created Frozen/Burning/Electrified fields last+1000 ms within global caps; no unit status duration or damage amplification. | frostline |
| seeker | Trailcraft | Own body sight+30 cm, subject to total+50 cm cap including equipment; no wall/smoke vision. | farwatch_ward |
| broker | Careful Assembly | Equipment assembly/quality fee reduced1 gold, minimum1; strongest with Broker Seal, never stacks; excludes Charter/books/commodities. | surety_shell |
| trickster | Convincing Decoy | False Trail decoy lasts+1000 ms; still no interactions, sight, rewards or combat hitbox blocking. | false_trail |
| laborer | Load Planning | Carrying capacity+4; applies to natural-capacity formula, no economic score or new gold. | legacy:Heave |
| rogue | Patient Pursuit | Same behavior as Infiltrator trait; preserve actual legacy starting skills and theft rules. | legacy:preserve |
| scholar | Field Practice | Same behavior as Arcanist trait; preserve legacy starting skills and utility costs. | legacy:preserve |

Six v0.7 teaching templates remain with equal53-point natural budgets and two Rank1 starters. Arcanist replaces Arc Bolt with Frostline at new-match creation to exercise water/frost; both remain purchasable. Other kit skills and no-free-gold/no-free-gear policy are unchanged. Legacy Laborer/Rogue/Scholar keep actual audited presets/skills and receive the listed one trait. Their skill IDs are unresolved adapter markers, not importable definitions. Any other delivered legacy class must receive an explicitly authored single nonstacking trait before enabling normal v0.8; do not silently map by display name. No subclass trait stacking. Trait field-duration/construct limits share the same global caps as equipment.
