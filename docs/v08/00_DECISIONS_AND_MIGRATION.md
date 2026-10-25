# v0.8 decisions, authority and migration

25 October 2026. User decisions govern: one class trait; bounded systemic environment; hybrid equipment; six common active slots; seven body equipment bindings; four accessory bindings. Rivers are always navigable, apply Wet, direct Frost briefly stuns Wet units, and frost on river creates a slowing surface. The following timing, budgets and content counts are selected implementation defaults, open to playtesting rather than previously approved balance.

Precedence: current user decisions → v0.8 normative prose/data → unchanged v0.7 rules → masterR7 long-term intent → frozen earlier sources. The two supplied source archives are included byte-for-byte under reference/. Extract v0.7 into a separate directory for inherited documents. Do not import its active config beside v0.8. No actual project source, engine version or delivered behavior was supplied, so audit actual baseline before implementation. A specification is not a verified game build.

| Topic | v0.7 | v0.8 governing rule |
|---|---|---|
| Learned/equipment action slots | Five skills | Six shared active slots, any combat/utility/profession; gear-granted body action also consumes one |
| Item access | Three quick references | Four physical accessory slots; active or passive accessories/consumables compete; no independent quick bar |
| Body slots | Weapon/offhand/body/feet | Head/torso/legs/feet/gloves/main hand/off hand |
| Hybrid equipment | Body active also needed quick reference | Body item stays equipped and grants an action into six-slot bar; accessory activates only from its own slot |
| Class identity | Starter template, no intrinsic rule | Exactly one permanent nonbreakable trait; signature remains replaceable regular active |
| Persistent passive skills | Possible legacy innate action | Persistent passives only equipped items and the one trait. Audit innate dash/passives; no free action bar |
| Rivers | Navigation concern/bridge proposal | Authored river floor always walkable. No Freeze bridge, melting collision, swimming or drowning |
| Wet + direct Frost | Not present |600 ms stun,6000 ms victim-wide lock, normal guard/immunity rules |
| Frozen surface | Not present |30% movement slow while inside; never stun on contact, no sliding physics |
| Generic hard-control DR | Off | Still off. Wet-stun lock is a specific reaction restriction, not generic stun DR |
| Silence | All skill actions | All learned active skills including martial/profession; gear actions count as items and are blocked by Mute |
| Victory/economy | Four routes, one ten-second claim | Preserved; damaging fields alone do not interrupt claim; Wet-stun/actual displacement do |

## Deterministic migration at a fresh match boundary

Never convert a live v0.7 match. Carry local UI preferences only: weapon→main_hand, offhand→off_hand, body→torso, feet→feet, charm_a/b→accessory_1/2. Existing quick consumables may fill remaining accessory_3/4 in prior reference order; conflicts overflow to unbound inventory with explicit review. A referenced body active becomes a source-instance action candidate in shared slot6 if available; do not move an existing learned skill silently. Unmapped active references become inactive, not deleted items. No passive from unbound inventory. Revalidate final count/weight/handedness and show the full result before commit. New matches normally start without transferred items; this is a development save/UI adapter, not a grant.

Retain all28 v0.7 ability IDs,40 item IDs,12 recipes and four capstones. Arcanist new-match teaching kit changes Arc Bolt→Frostline; no existing persistent learner loses knowledge. Add24 abilities,40 items,8 optional recipes,9 trait definitions including three known legacy classes. Preserve actual legacy skills, costs, item sources and classes through audited adapters; unknown legacy classes need a single explicit trait before enabling. No inferred deletion or renaming.

Default keys Q/W/E/R/D/G and accessories1/2/3/4; A attack, F context, T trade, S Stop. G avoids known F/T/A/S controls but must undergo local binding collision checks. Labels/remapping are configurable; no hidden control shortcut. Out-of-match v0.6/v0.7 regression profiles stay isolated, never resource sources in v0.8.

Keep v0.7 integration duration2100s,1–4 locked contenders, level15,24 carried instances,53-point starts,8–16 natural attributes,35-minute economy tick schedule unchanged. Full60–90-minute geography/pacing remains future design. Seven body plus four accessories is eleven equipped instances maximum; these count inside24, not extra storage. One two-handed item occupies two bindings but one instance. No new rare-key/boss/tower quantities.
