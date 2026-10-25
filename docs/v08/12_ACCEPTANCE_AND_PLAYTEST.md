# Acceptance and playtest cases

All new cases below are required, plus all 55 inherited v0.7 cases and affected v0.6 E01–E48 regressions. Rebase inherited slot/catalog expectations to explicit v0.8 decisions; never run contradictory old 5/3/6 profile assertions against v0.8. Record case IDs, build hash, profile, server receipts, client video/logs and result. Fixtures isolate edges; normal four-route evidence uses legal acquisition. The status of every native case in this package is Not run.

| ID / key | Setup/action | Expected |
|---|---|---|
| V08-001 / slots_ten | Equip six learned skills plus four active accessories; attempt an eleventh via direct request. | Ten legal actions, extra source rejected without spend. |
| V08-002 / body_active | Equip Reflective Mail, leave it off bar, then allocate slot6. | No reflection before allocation; allocation consumes one of six; no seventh learned action. |
| V08-003 / body_mute | Silence actor with gear reflection on shared bar, then Mute actor. | Silence allows gear action; Mute blocks it; learned martial has reverse classification. |
| V08-004 / source_unbind | Quiet-unbind a source with a granted action; resend stale action request. | Dependent bar ref removed atomically; stale request rejects. |
| V08-005 / consumable_no_refill | Use bound Water Flask while another sits in bag. | One identity consumed, binding empty, no automatic bag refill. |
| V08-006 / passive_no_button | Equip four passive accessories then request backpack Smoke Flask activation. | Passive effects work; bag action rejects. |
| V08-007 / two_hands | Equip halberd then shield simultaneously. | Draft rejects; halberd counts one item, occupies both hand bindings. |
| V08-008 / inventory24 | Fill24 carried identities including eleven equipped; pick up another. | Count/weight once; pickup rejects, does not create extra gear storage. |
| V08-009 / stat_caps | Request excessive positive stats across all 11 bindings. | Deterministic head-first allocation,+2piece/+3attribute/+6total, excess preview matches server. |
| V08-010 / trait_one | Try combine two traits or respec at station midmatch. | Exactly one starting immutable trait, no new button, no second passive. |
| V08-011 / trait_break | Apply Break to each class trait and breakable item simultaneously. | Trait remains; tagged item proc/behavior suspended, stats and action access remain. |
| V08-012 / signature | Replace signature with a universal learned skill. | No lost trait, no forced signature slot, no exclusive ordinary book gate. |
| V08-013 / legacy_adapter | Load Laborer/Rogue/Scholar and an unknown actual legacy class. | Known trait/skill mapping preserved; unknown adapter blocks normal enablement until authored. |
| V08-014 / cooldown_swap | Use item then quiet swap/drop/trade/reacquire/death. | Maximum owner-family/instance endpoints retained, no immediate second cast. |
| V08-015 / quiet_cancel | Start loadout commit then move, take damage, use item or lose focus. | No partial gear/action edits; prior loadout remains. |
| V08-016 / river_walk | Right-click player and AI across authored river before freeze, during freeze, after melt. | Same graph/path remains usable, no collision/nav rebuild/no swimming. |
| V08-017 / wet_entry | Enter river and leave at exact boundary. | Immediate Wet;100 ms refresh;3000 ms residual,1500 with Waders;state expiry exact. |
| V08-018 / river_frozen_wet | Freeze river then walk across it. | Still gains Wet;30%slow or15%withTreads;never contact stun. |
| V08-019 / wet_frost | Hit Wet enemy with Frostline once. | 600 ms stun if unguarded;Wet consumed;victim readiness now+6000;one receipt. |
| V08-020 / dry_frost | Hit dry target with Frostline outside wet cells. | Damage without reaction stun;no imaginary Wet prerequisite granted. |
| V08-021 / frost_capsule | Create Frozen surface under Wet target and remain inside10ticks. | Slow only;zero direct-Frost attempts;no stun. |
| V08-022 / reaction_shared | Two casters and Frost Prism target same continuallyWet victim within6s. | At most first eligible reaction;lock shared across sources, not extended by rejected later attempts. |
| V08-023 / reaction_endpoint | First attempt1000;new hits6999 and7000 whileWet. | 6999no reaction/extension;7000eligible. |
| V08-024 / reaction_guard | Preapply Control Guard then valid Wet–Frost hit during claim. | Guard consumed, damage applies, Wet consumed,6 s lock;no Stun and claim continues if alive. |
| V08-025 / reaction_block | Spell-block a unit-target Frost Prism packet. | No damage, Wet consumption, guard consumption or reactionlock;area Frostline bypasses spellblock. |
| V08-026 / reaction_death | Die/rejoin after reaction but before readiness. | Readiness persists;ordinary status clears;river can Wet new life but lock still applies. |
| V08-027 / category_immunity | Direct Frost Wet tower/boss/protected official. | Public target categories respected;no immune-category stun or service damage. |
| V08-028 / self_hazards | Caster stands in own fire/electrified/steam/ice. | Neutral field effects apply;direct attack self immunity does not extend to environment. |
| V08-029 / melt | Fire enters Frozen river cells. | Frozen clears;Wet+Steam3s;no Burning same transition, no navigation change. |
| V08-030 / steam_symmetry | Place smoke/steam across defender/caster/ward. | Same union boundary occlusion rules both sides;ward only own legitimate sight. |
| V08-031 / gust_clear | Gust a subset of cloud cells. | Only actual mask clears;authorization recalculates immediately, no wall reveal. |
| V08-032 / water_lightning | Lightning wet/river and dry cells with same footprint. | Only conductive nonFrozen cells electrify;no chain beyond mask, no stun. |
| V08-033 / chain_observation | Chain Lightning primary observed but next candidate hidden behind smoke. | Hidden candidate never chosen;up to3 distinct visible lives, wet+20 only when present. |
| V08-034 / oil_fire | OilFlask on dry ground then Fire. | Oil consumed, Burning6s;normal20DPS, no duplicate bonus ticks. |
| V08-035 / water_extinguish | Apply Water to Burning unit/field. | Burning clears, unitWet;already applied damage not rewound;Oil not restored. |
| V08-036 / nature_fire | Entangle soil then Fire. | Root from direct skill separate;growth clears toBurning;static trees/collision/sight unchanged. |
| V08-037 / shatter | Shatter Frozen cells under one body overlapping several cells. | One80base physical hit/life/cast;ice clears;no Wet stun/no path collapse. |
| V08-038 / surface_tick | Stand on overlapping samekind sources;enter just before tick. | Strongest samekind once per500 ms;no full duration damage per callback. |
| V08-039 / burn_double | Apply unit Burning and Burning surface simultaneously. | Greater20DPS source, not32;ordinary mitigation/caps respected. |
| V08-040 / field_caps | Fill owner64/match256 including reservations, request newfield. | Precommit generic reject without cost;no other owner field deletion. |
| V08-041 / paid_projectile | Pay Firebolt field reservation then reflect/block/miss. | Impact uses reserved sponsor capacity;block/miss releases once;reflection does not overallocate. |
| V08-042 / transition_budget | Queue more than512candidate transitions on one100 ms boundary. | New requests reject deterministically before pay;expiry/CC/Heart timing not delayed. |
| V08-043 / field_replacement | Replace another source state on same cell and expire it. | New attribution/revision and owner allocation correct;no leaked/double capacity. |
| V08-044 / field_death | Kill source with live cellfields and wards/conduit/gates. | Fields expire normally;owned emitters specified vanish;no cloned item/no old-life target retarget. |
| V08-045 / duration_caps | Stack Arcanist, Frost Gloves, Resonance and field lifetime. | One eligible bonus per family then state cap;no unit Stun bonus. |
| V08-046 / proc_break | EmberWand/ConductiveSpear proc;Break;swap same family. | Deterministic procs, no recursion;Break suspends/no resets;cooldown endpoints persist. |
| V08-047 / mod_caps | Combine StormCrown/Insulation/MR and GuardianShield/armor. | Strongest conditional family;total perkind mitigation<=60%;no multiplicative immunity. |
| V08-048 / aura_consent | CommandBanner near stranger then grant/revoke aid. | Only self/consented get4armor, strongest only;no ordinary allegiance inference. |
| V08-049 / support_pulse | RallyGuard once with consenting and nonconsenting units. | One barrier pulse on eligible bodies;no learned enduring aura. |
| V08-050 / sabotage_scope | Sabotage ward, conduit, ground anchor, service, tower, Heart. | Only legal construct disabled;nonobjective emitter resumes after3s if alive;service/objective immune. |
| V08-051 / catalog_import | Import every 52 abilities/80 items/20 recipes/modifier and operation. | All IDs/fields resolve, real handlers/assets;no silent ignored field or log-only active. |
| V08-052 / acquisition_all | Resolve source of every item/book and complete example optional recipes. | Priced creation or actual named recipe/drop;null not free;all paths reachable without grants. |
| V08-053 / quality | Upgrade permitted weapon/armor/barrier then try proc-only accessory. | Onlyquality_fields scale;ineligible rejects/no charge;same identity/cooldowns. |
| V08-054 / rare_source | Reset caches/boss/towers;loot/read/recreate actor. | Three keys/catalysts, one assigned capstone each, three relic receipts;no extra rare spawn. |
| V08-055 / gold_conservation | Buy/craft/quality/trade/death with expandedcatalog. | Ledgers reconcile;fees/prices/tax burns;no new rewardmint beyond540*N+682. |
| V08-056 / claim_damage | Claim inWet river while taking nonlethal Fire/Lightningfield ticks. | No cancellation fromdamage/Wet/slow alone. |
| V08-057 / claim_frost | At claim completion timestamp apply valid Wet–Frost reaction. | AppliedStun cancels beforecompletion;guarded/suppressed reaction does not. |
| V08-058 / claim_ice | IceCapsule/Rain/Steam a stationary claimant. | No direct interrupt;vision/access still independent;publicstatic sensor remains. |
| V08-059 / claim_force | Gust/Rescue/Anchor atclaim. | Any actual forcedmovement interrupts;zero movement alone doesnot;Anchor onlypushpull. |
| V08-060 / routes_normal | CompleteSiege, Charter, Keys, Artifact separately using normal earnings/items. | One10sclaim perroute, all inherited access/endpoint/deadline rules;no grant proof. |
| V08-061 / privacy_payload | Inspect unauthorized clients during hidden fields/procs/target hits. | No hiddenactor/build/owner coords;permitted staticobjective warningonly. |
| V08-062 / ground_oracle | Cast blind groundfield with hiddentarget nearby;inspect response. | Genericcapacity/placement feedback, no victimcount/hiddenstatus lookup. |
| V08-063 / ui_sources | Inspect self/enemy, remapkeys, consumeacc/unbindbody. | Sourcebadges/disabledreason accurate;enemyUnknown;no binding collision silently overwritten. |
| V08-064 / ai_river | AI contender crosses river and attempts observedWetcombo. | Samecost/slots/permissions;no hiddenstate cheat, no hard navigation block from hazards. |
| V08-065 / npc_peace | Run ordinary resident production/trade while contenders fight. | Workcontinues, officialservice protected;no NPC-widecombat conversion. |
| V08-066 / performance | Run4clients,256dynamiccells, reservations/expiry for10 minutes on recordedmachine. | Measure service p95<=2 ms/p99<=5 ms goal;logs show no unbounded growth/permission leak. |
| V08-067 / lower_level | Use affordable counter/control to interrupt strongerclaimant after legal acquisition. | Tacticalimpact observed, no requirement towin exposedduel;report actual build/income. |
| V08-068 / human_readability | Humanplayers distinguishWet/directFrost/Frozensurface/Steam andbind gearactions. | Record confusions and observedTTK/controlchain;missing participants=Notrun. |

Human measurements: committed-fight TTK target8–15s; cumulative hard-control time; frequency/impact of Wet reaction; field readability; skill vs item source classification; build diversity among all 11 gear bindings; accessorypassive vs active tradeoff; time/gold to counters; lowlevel interruption;economyroute feasibility. These are questions to measure, not validated balance claims. Never infer missing engine/network/human results from Pythonmodel success.
