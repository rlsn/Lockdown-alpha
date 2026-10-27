#!/usr/bin/env python3
"""Rebuild the native acceptance index and blank implementation templates."""
from pathlib import Path
import csv,json
ROOT=Path(__file__).resolve().parents[1]
CASES={
'B90':[
('Import the selected full profile twice, then reset','96 POIs,144 buildings,24 spawn pads; stable IDs; no duplicate actor/source/market','Import report, source hashes, actor ID diff'),
('Walk without abilities from every spawn to every service and objective','Native path succeeds; no compulsory ladder, bridge-only river gate or invalid slope','Native all-pairs required-target path report'),
('Walk all named and unmarked river crossings thawed and frozen','Same connectivity; Wet at fords, dry deck over bridge; no base river slow','Movement/contact logs and capture'),
('Traverse every road/plateau interface','Continuous <=25% ramp or valid stairs, correct band height, no cliff clipping','Native sweeps, elevation overlay'),
('Inspect routes to each ruin, boss,forge and Heart','Two usable approach branches at key targets; one occluded or alternate branch','Native paths + sightline evidence'),
('Walk antechamber to Heart at420 cm/s','Measured <=15 seconds on normal dry path; claim circle not forced Wet','Path trace and server elapsed time'),
('Visit five cities and six towns','Correct identity/services, six city districts, distinct watched core and edge','Level walkthrough + service mapping'),
('Regenerate seed9001 twice and reopen level','Same semantic IDs, positions,source counts; visual scatter cannot move objectives','Serialized manifest diff'),
('Place all primitive building/road geometry','Local district approach routes fit doorways; no carved road through a closed shell','Native collision and path overlay'),
('Walk near playable border and ledges with push/dash','No falling death, out-of-bounds launch or endpoint inside collision','Sweep and rejected endpoint receipts'),
('Measure prescribed long trips and empty road stretches','Native length/time separated from graph estimate; empty segments recorded for density tuning','Completed travel CSV and heat trace'),
('Switch fresh match to v08_integration','Original short-map limits and sources active alone; no v09 rewards/endowments imported','Profile-specific actor and ledger dump')],
'B91':[
('Enter all36 interiors; inspect108 closed shells','Exactly25% enterable; legal real doors/stairs, no fake door verbs','Native interior checklist'),
('Open/close door while another client observes and paths through','Collision,sight and local navigation share one revision; occupied closure rejects','Two-client video + revision receipts'),
('Force/lockpick rear door; attempt lock at public front','Rear broken-open persists; no public-service lockout; owner rule enforced','Door and navigation logs'),
('Two clients take one flask or carry one ladder simultaneously','Exactly one location/owner; losing request spends nothing','Race receipts + item identity query'),
('Throw/pour a flask from unbound inventory then bound accessory under Mute','Unbound/Mute rejects; bound action consumes once with inherited cooldown/mask','Action/inventory receipts'),
('Ignite torch and toggle light through drop/swap/death/rejoin','Fuel never resets; no extra sight/detection or ground auto-fire; Mute respected','Fuel trace + restricted payload'),
('Carry,push,place crate/barrel near door/Heart/corridor and another body','One world instance,70%carry speed,suppressed actions; unsafe placement rejects; no body displacement','Native geometry + receipts'),
('Break an empty and a filled container in a race','Existing contents drop once; no invented oil,material,gold or duplicate storage','Lineage/item identity check'),
('Deploy/retrieve rope and ladder; race traversal; Root/Stun mid-use','One instance/anchor reservation; permanent stairs usable; interruption returns valid endpoint','Link/inventory/state logs'),
('Harvest,burn and attack the same sapling across death/reload','At most one Fiber when harvested; destruction none; proxy removal matches sight/nav','Source receipt + geometry diff'),
('Hide in grass, approach within2 m, detect, attack, then burn patch','Geometry-gated concealment; reveal timers; no wall reveal; burnt cover stays removed','Two restricted client traces'),
('Use Wet+direct Frost repeatedly on river, then Frost Capsule alone','600 ms stun/6000 ms victim lock; floor only30%slow; guard and death lock rules retained','Combat/contact boundary trace'),
('Create Steam then Gust; attempt unit target across smoke/wall','Union occlusion applied to all audiences; authorization recalculated; no target/private-data leak','Restricted serialized payload inspection'),
('Fill field budget while lighting sapling/oil','Capacity refusal without cost; paid reservations survive; expiry and source ownership exact','Capacity/receipt trace'),
('Fit all480 functional props and3000 scenery-tree targets','No essential corridor overlap; all verbs work; static trees do not burn or drop loot','Fitted manifest + interaction sampling + count report')],
'B92':[
('Start default4-human full match then12-human configuration','196 noncontenders +24 contenders;20 then12 AI contenders; no double roster count','Roster/role budget dump'),
('Observe civilian loops and cross-city travelers for15 minutes','Real path travel, staggered activity,flee/resume; sealed cargo neither loot nor market stock','Routine timeline + stock delta'),
('Attack resident/force rear door inside and outside guard sight','Only observed local incident; no omniscient suspect or human label','Guard knowledge and observer records'),
('Fight/respawn guards and ambient wildlife repeatedly','Zero gold/XP/loot mint;24 guard IDs;90 s downtime; no infinite reinforcements','Kill/source and ledger receipts'),
('Buy/sell simultaneously at all five market pools','Current unit quotes,finite cash,stock caps and source lineage; stale quote rejects','Transaction races + five-pool ledger'),
('Buy Circle-III book at multiple official endpoints','Only two copies per definition globally; no per-city rare stock clone','Stock/source audit'),
('Run all29 production/consumption ticks including empty/poor pool','At most145 producer burn; actual items consumed; no deferred catch-up or hidden funding','Scheduled receipts + identity closure'),
('Complete/repeat/abandon/die during courier and procurement work','20 paid courier cap,40 issuance cap,one active token; endorsement andN budgets exact','Contract/source receipts'),
('Harvest all deposits,saplings and initial common caches','Finite charges/endowments,no XP/gold mint or life reset; no rare key in common chest','Item-source closure'),
('Progress from zero kills to paid registered Charter with a legal Witness path','Actual native skill gates/costs documented; no free grant; both endorsements and100 gold spent','Complete action and balance trace'),
('Transfer gold,bank across cities,die with gear/cargo/consumables','One account; fees/tax burn; ordinary gear retained; consumables/objectives drop once','Gold and item conservation report'),
('Hide hazards/keys/rival wallet from contender AI','AI choices use owned/observed/public facts; no remote path cost or objective-carrier oracle','AI input audit + restricted view'),
('Move near/far bodies across simulation tiers during combat/transactions','Wake before observation; one identity/receipt; no skipped damage or visible teleport','Tier transition and native movement trace')],
'B93':[
('Destroy each tower once then reload/rejoin','Three permanent losses,three relics,total120 gold; siege opens once; local watch removed','Objective/source/access log'),
('Activate Charter near Heart while a different contender waits locally','30/60 public window; nonbuyer can physically win; Treasury purchase still required','Activation/travel/claim receipts'),
('Collect all keys,activate,transfer/drop/recover one','Unique keys,lease revoked on loss;90 s instance+owner locks persist','Instance and access timeline'),
('Craft/activate Heartbreaker,die before and after activation','Exact inputs/20 fee,8 s craft; artifact drops precommit or is consumed once; no refunded lease','Lineage and life-bound lease receipts'),
('Attempt duplicate ruin,boss,Fragment or capstone reward','Three key bundles,one boss,finite assigned books; restored body cannot pay again','Source identity closure'),
('Hold Heart through ordinary damage,Wet,Root,Silence,Mute and proximity','Surviving authorized claim continues; no passive contest or damage interrupt','Claim cancellation reason log'),
('Apply Stun,Hex,Interrupt,actual push and zero-distance blocked push at completion','Actual qualifying effects cancel; zero movement alone does not; due combat precedes completion','Scheduled-time boundary trace'),
('Use guarded Wet-Frost and frozen surface at a claim','Guarded no-Stun and surface slow do not falsely cancel; lock spent as inherited','Reaction and claim receipts'),
('Overlap access routes; close one exactly at claim end','OR authorization; inclusive endpoint; no timer extension or boolean-close race','Access interval/claim receipt'),
('Start two claims with equal completion time; reach exact deadline','One role-neutral start-ordinal winner; valid due completion before NoAcquisition','Winner receipt + monotonic timeline'),
('Attack,observe and approach Heart without own access route','Defenders enter normally; fog still governs targeting; no civic guard/shrine/protection bubble','Native precinct inspection'),
('Run AI route attempts using real earned resources','No hidden teleport,free key/artifact/Witness,money or scripted tower deletion','Complete contender action/commerce traces')],
'B94':[
('Run normal24-contender matches at60/75/90-minute deadlines','Terminal winner or truthful NoAcquisition; no min-win gate; correct timer/profile','Per-run roster,profile and terminal receipts'),
('Run at least one human-involved60+ minute observed session','Record pacing,empty travel,first encounters,route response; no automated-soak balance claim','Session notes + telemetry'),
('Stress distributed whole-map population then capital convergence','Recorded hardware/settings,30 Hz server p95<=33.3 ms,p99<=50 ms;60 FPS target measured','Profiler captures and percentiles'),
('Stress1024 live cells,2048 transitions/100 ms and pending reservations','No paid field loss,resource overrun,late claim/control reorder or expiry drift','Environment and event queue profiler'),
('Test150 ms RTT,packet loss,reordered/duplicate requests and disconnect/rejoin','No stale-life commits,duplicate item/reward or identity leak; p95 interaction target reported','Network harness and receipt audit'),
('Open remote inspector/minimap/listeners behind fog during mutations','No hidden roles,build,wallet,loot coordinates,crime tracker or live fogged proxy changes','Restricted payload diff'),
('Soak carry,door,nav and traversal interactions with crowd pressure','No permanent path deadlock,stuck essential service,vanished cargo or repeated receipt','Path failure/timeout logs'),
('Close every gold and item ledger at terminal/reset','Delta zero; initial+mint-burn accounting includes all market/resident/world locations','Machine-readable closure report'),
('Regress inherited52 abilities/80 items/20 recipes and traits/loadouts','No map-scope deletions,fake ops,unbound item actions or changed combat limits','Inherited test report + importer coverage'),
('Review all B90-B94 exits and deviations','Native evidence attached; failed/not-run gates remain visible; tuning/data/prose synchronized','Completed implementation report and feature ledger')]
}
rows=[]
for stage,cases in CASES.items():
    for i,(setup,expected,evidence) in enumerate(cases,1):rows.append({'id':f'{stage}-T{i:02d}','stage_id':stage,'action':setup,'expected':expected,'required_evidence':evidence,'native_status':'Not run'})
(ROOT/'data/acceptance_index.json').write_text(json.dumps({'schema_version':9,'rows':rows},indent=2)+'\n')
lines=['# Native acceptance and playtests','',f'{len(rows)} native cases. All initial statuses are **Not run**. `tools/validate_spec.py` checks package consistency and selected independent boundary models only; it cannot pass these native cases. IDs use the user-required B90–B94 stages.','', 'Record actual evidence and failures in the implementation report; do not edit this source index to imply test execution.','']
for stage in CASES:
    lines.extend([f'## {stage}','','| Case | Action | Required result | Evidence |','| --- | --- | --- | --- |'])
    for r in rows:
        if r['stage_id']==stage:lines.append('| '+' | '.join([r['id'],r['action'],r['expected'],r['required_evidence']])+' |')
    lines.append('')
(ROOT/'10_ACCEPTANCE_AND_PLAYTEST.md').write_text('\n'.join(lines))
with (ROOT/'templates/FEATURE_LEDGER.csv').open('w',newline='') as f:
    w=csv.writer(f);w.writerow(['feature_id','stage_id','description','status','source_commit','acceptance_case','evidence','deviation'])
    for stage,cases in CASES.items():
        for i,(action,_,_) in enumerate(cases,1):w.writerow([f'{stage}-F{i:02d}',stage,action,'Not started','',f'{stage}-T{i:02d}','',''])
(ROOT/'templates/IMPLEMENTATION_REPORT.md').write_text('''# v0.9 native implementation report

Status: Not started. This blank template contains no runtime evidence.

## Baseline

- Repository / source commit:
- Engine version / platforms / build configuration:
- Build / launch / test commands:
- Existing movement, capsule, fog, navigation, interactions, combat, inventory and UI architecture:
- Actual starter wallet/kit and inherited Witness operation/gates:
- Source archive hashes / active profile / seed / fitted layout revision:

## Stage status

| Stage | Status | Commits | Native evidence | Failed / not run cases |
| --- | --- | --- | --- | --- |
| B90 | Not started | | | |
| B91 | Not started | | | |
| B92 | Not started | | | |
| B93 | Not started | | | |
| B94 | Not started | | | |

## Measured results

- Native path/reachability and travel table:
- Fitted POI/prop/visibility/nav consistency:
- Restricted-client / AI sensory authorization:
- Gold/item/source closure:
- NPC routines/security/roster and wake tiers:
- Four route attempts, claim endpoints and body/life receipt races:
- 60/75/90-minute runs, normal/fixture labels, humans/AI and terminal results:
- Hardware, resolution, settings, RTT, server/client percentiles and worst-density profile:
- Human pacing: travel/idle share, first rival sighting, levels/gold, route competition:

## Deviations and remaining work

List exact changed defaults and synchronized data/prose/assets. Separate known failures, unimplemented behavior, not-run checks, external dependencies and proposed tuning. A static imported marker or spec check is not native completion evidence.
''')
print(f'Wrote {len(rows)} native acceptance cases and B90-B94 templates.')
