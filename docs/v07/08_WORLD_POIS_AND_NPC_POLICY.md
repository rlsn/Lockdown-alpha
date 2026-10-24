# World, points of interest and NPC policy

## Authored region

Extend the actual City/WestTown/EastTown region rather than requiring four complete cities. `data/world_and_routes.json` specifies nodes and required links, not literal coordinates. Use native assets/navmesh and measure traversal under normal movement. Every Heart entrance must have at least two viable approach paths, including one occluded flank; no single permanent tower beam can observe them all. Services and rare rewards sit on meaningful routes, not in one debug room.

| POI | Normal role | Contest and information |
| --- | --- | --- |
| City / Treasury / Guild | Reliable counters, bank, contracts, public Charter | Marked services protected, ordinary bodies still vulnerable |
| West / East towns | Finite markets, trainers, workshops, preparation | Public road vs covered route; physical service range |
| Three camps | Bounded combat XP/gold, spatial combat rehearsal | 12 authored spawn identities each, no body-reset rewards |
| Three ruins | Distinct key, catalyst, named capstone book | Shared physical cache, sight/quiet channel; all contents transferable |
| East advanced forge | Specialized gear and Masterwork work | Transport and work-order costs, interruptible preparation |
| HeartForge | One Heartbreaker assembly recipe | Eight-second physical vulnerable craft, no automatic access |
| Guardian boss lair | One Fragment/book drop and bounded reward | Telegraph, finite source, rival interruption |
| Shrines | Recover HP/MP after disengagement | Four-second hold, six-second quiet, 90-second owner cooldown, no XP/gold |
| Three Grand towers | Siege gate and local surveillance | Destroy permanently, lose corresponding public source, drop contestable relic |
| Heart antechamber | Keys/artifact activation outside claim radius | Public timed warning, not secret instant bypass |
| Heart precinct | One ten-second claim | Public attempt sensor; opponents must act, proximity alone does not stall |

## Camps, boss and tower interactions

Three camp spawners each have twelve match-scoped spawn IDs. First is available at reset; subsequent may spawn at least150 seconds after previous spawn and only after prior creature is dead. At most one living creature per camp. Endowment/source capacity is not already-created live items. Each eligible death creates one 12-gold/90-XP reward once for last valid damaging contender. No spawn after terminal match state and no catch-up beyond match deadline. No reward for killing the same restored body twice. Optional flee/leash returns heal without refreshing spawn/reward identity.

Guardian boss has2600 HP,15 armor,20 percent MR,90 physical attack every1.4 s. Every10 s it telegraphs a ground pulse for1.5 s, then deals120 magical damage within400 cm. Telegraph is observed through normal visibility. Melee range220 cm, leash1000 cm; leaving resets HP/position but never source/reward budgets. Defeat pays90 gold/250 XP once and creates the named book/Fragment drops. No hard-control/displacement. Avoid calling this a rich boss AI: baseline is a readable bounded encounter. It need not require a kill by the eventual artifact crafter, because drops can be bought/stolen.

Grand towers are independently vulnerable from match start in v0.7; no new outer-tower prerequisite. Each1800 HP,20armor,25 percent MR; observed eligible hostiles within650 cm are attacked for80physical every1.5 s. Selection uses nearest visible hostile, stable spawn ID on tie; attacks do not home through fog. Protected official services/decoys excluded. Destroying each removes its local surveillance source, opens its authored approach obstruction/shortcut, pays40 gold/150 XP to valid last hitter and drops one Tower Relic at the tower. Last hitter receives first normal pickup opportunity by proximity, not invulnerable exclusive loot; no automatic safe equipment grant. Relic always drops thereafter. At all three down, Heart siege authorization becomes global permanently. Other routes can work while all towers stand. Surveillance loss cannot erase every town service or economic route.

Tower/boss ordinary HP/damage values are provisional tuning. A public structure target category explains disable immunity; no hidden human-dependent exception. Last-hit ownership uses released attack/DoT source persistent contender+spawn identity. If a source is disconnected/dead, budget may still pay its persistent contender once; no resurrected body re-earn. If no eligible contender caused the death, no personal gold/XP reward; objective destruction/drop still occurs once.

## NPC scope

Preserve peaceful routines and crowd cover. Marked official institutions are public nonwinning services. Residents are neither labelled contenders nor humans. Their killing grants no fresh XP/gold. Ordinary contender NPCs, if actually present, share slots, resources, loot/death, natural gates and access rules. Their decision inputs are only owned/observed/public facts; no human flag, hidden purse or private book list.

Do not require a complete autonomous four-route strategist before combat release. If baseline has strategic contenders, extend its policy to choose legal available POIs and use effects through normal commands. If absent, a labelled optional AI test opponent may exercise combat legality but is not evidence of a naturally self-progressing rival world. Keep source capacity/rewards fixed from actual locked N. No scripted tower deletion, teleport to hidden key, free capstone or unearned Charter in normal play.

## Readability and travel evidence

Measure ordinary travel City→each town, town→ruin, shrine→precinct, and antechamber→claim circle. The mandatory30-second warning must provide an observed rival a plausible response route; if measured travel makes every defense impossible, adjust placement or warned-route timing together and record the change. Warning is not a promise every player everywhere can arrive in time. Put shrine/cover outside instant tower range; no fountain heal inside capture. Verify actual native occlusion/navlinks, not only the graph.
