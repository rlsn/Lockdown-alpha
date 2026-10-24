# Acceptance and playtest matrix

Planned game checks, not completed results. C001–C055 are required or explicitly Not run/Failed with reason. Preserve valid unchanged predecessor evidence; rerun impacted behavior. Engineering tests, native/network tests and human judgments remain distinct.

| ID | Area | Required evidence |
| --- | --- | --- |
| C001 | baseline | Actual v0.6 commit, toolchain, source inventory, legacy class/skill/Witness adapters and E01–E48 gaps recorded; no assumed delivery |
| C002 | profile | v06 regression and v07 integration cannot mix; 35min/15 cap/5skills/3quick configured and clean restart preserves no prior-match state |
| C003 | input | Q/W/E/R/D,1/2/3, A/F/T/S remap conflicts, removed quick4, modal focus and no click fallthrough; self commands persist on enemy inspection |
| C004 | resource | New skills consume real MP only on accepted release; cancel recovery; no refund on spell block; replay/stale/old-life requests spend once or none |
| C005 | damage | Physical/magical formulas, barrier-before-HP,60 percent mitigation cap,40 percent barrier cap and simultaneous traded kills have exact receipts |
| C006 | status | Silence blocks Purge Touch but allows Null Bell; mute reverses; stun/hex block both; external consent Remedy removes eligible hard tags |
| C007 | status | Root blocks walk/dash/blink/gate but allows spells/attacks and incoming push; silence/mute/root alone do not cancel stationary Heart |
| C008 | stack | Same-status overlap expires at maximum source endpoint, not summed duration; strongest slow, source aura reapplication and dispel provenance correct |
| C009 | protection | Spell-block charge, control guard, reflection once, AoE bypass, dome crossing and eight-second major-protection lock differ correctly |
| C010 | passives | Break only disables breakable passives, never natural gates/gear stats/capacity; blind affects own source, wards remain independently authorized |
| C011 | vision | Fog/smoke/walls/elevation/detection do not leak identities or private stats; hidden homing/impact cues and Ledger Mark cannot reveal unseen bodies |
| C012 | geometry | Push stops at wall/ledge, actual nonzero movement interrupts; blink/swap/gates validate endpoints; invalid placement reveals no hidden coordinate |
| C013 | build | Natural/effective split rejects INT9+gear Temporal Lock; cross-class natural investment eventually permits legal equip/cast |
| C014 | build | Five shared skill slots include utility/context F; one Circle IV maximum; old innate/direct request shortcuts cannot bypass slots |
| C015 | mastery | XP thresholds, level-point budgets, rank 2/rank 3 costs and station commits correct; duplicate book keeps item and grants no rank/XP |
| C016 | potency | One potency rank capped3 affects only declared magnitude fields, never hard-disable duration/cooldown/gates/unknown skills |
| C017 | gear | Six bindings, two-hand conflict, one instance count/weight, deterministic +6total/+3stat clipping and negative tradeoffs match preview |
| C018 | loadout | Six-second quiet plus two-second binding, interrupted holds, all quick-slot entry points, no inventory-click item bypass or cooldown reset |
| C019 | capacity | Removing capacity gear while overloaded rejects; net craft/trade count/weight legal after outgoing inputs; reservations counted once |
| C020 | quality | Standard/Fine/Masterwork modify only eligible numbers; assembly resets quality with preview; quality same ID keeps cooldown endpoints |
| C021 | supply | Paid official counter/equipment/books create once/burn exact cost; unlimited essentials cannot be cornered; no new buyback mint |
| C022 | source | Circle III stock per-definition2, each capstone/key/catalyst source fixed, physical cache pickup/read/drop unique with no rejoin refresh |
| C023 | recipe | Every recipe/work order has exact inputs/fee/station/time, net capacity, reservations and lineage; staged allocation failure spends nothing |
| C024 | craft | Harpoon and three staff branches work; scarce catalyst required for completed work order; coupons/Seal apply only named fees; no disassembly |
| C025 | trade | Consent exact revision/digest, unbind-before-offer, retained item/family endpoints, objective transfer/revocation; death releases lock before drop |
| C026 | content | Every one of28 new abilities has legal source, actual effects, ranks, counterplay and visibility; not a tooltip-only implementation |
| C027 | content | Every one of40 catalog items has passive/active/drop/cost/quality behavior and source, including always-drop Tower Relic |
| C028 | special | Guard Swap two-stage cost/lifetime/CD, Grand Exchange atomic two-target block, Gate Weave visibility/endpoints/travel lock/cargo/death correct |
| C029 | world | Camp36spawn and boss/tower reward identities bounded, no leash/respawn refresh; class/build-hidden ordinary residents preserved; officials identifiable |
| C030 | world | Three ruins/forge/shrines/tower shortcuts and surveillance loss exist in native geometry; measured alternate approaches and warning travel |
| C031 | progress | Existing work/procurement and new PvE/discovery/PvP budgets grant XP once; resident kills/trade/craft/gather loops yield no new XP |
| C032 | death | Equipped ordinary gear retained; spare gear/books/cargo/consumables/keys/relic/fragment/artifact drop once; bank and knowledge persist |
| C033 | respawn | Fixed delay endpoint, three-second protection cannot interact/claim; HP/MP refill explicit, no free item/cooldown/budget reset |
| C034 | siege | All three towers legally destroyed opens access globally permanently, removes sources and pays/drops once; no fake deletion or other-route gate |
| C035 | charter | Normal zero-kill bounded economy earns Witness/endorsements/fee, activates public30/60 and completes attempt; registration/death recovery retained |
| C036 | charter | A contender without Charter wins its public opening; purchaser death leaves opening active; no private Charter/legacy Seal RPC bypass |
| C037 | keys | Normal acquisition/trade/theft obtains three distinct keys; four-second activation/30+60 lease, transfer/loss/death revokes and shared90 s item cooldown survives transfer |
| C038 | artifact | Normal component acquisition/HeartForge builds Heartbreaker; activation consumes once,30+60 same-life lease; death cancels with no refund |
| C039 | claim | Ten-second claim: damage alone/proximity do not interrupt; Counterpulse/ForceWave/stun/hex/lethal damage/self-cancel do; no actions while holding |
| C040 | claim | Private owner permission does not stop rivals entering/interfering; own concealment breaks; no blanket protected claim; every route ends in same claim |
| C041 | timing | Exact close/deadline completion legal,1 ms late rejected, due lethal/disable wins at tie, simultaneous claims tie by start ordinal, stalled tick drains scheduled order |
| C042 | access | Charter/private/siege overlaps OR correctly, route loss only cancels if no remaining authorization; private lease cannot extend global timer |
| C043 | deadline | Full warning/open eligibility validated; late activation no consume; no refund delayed purchased Charter; NoAcquisition at cap without hidden-score win |
| C044 | terminal | One winner across duplicate/completing claims, death, tower destruction and deadline; later transactions rejected, reservations/temporary sources cleaned |
| C045 | ledger | Gold end=start+mint−burn across wallets/banks/markets; reward upper bound540N+682; no transfer counted as mint; initial endowment once |
| C046 | ledger | Per-definition counts and parent lineage across equipped/cache/dropped/reserved/consumed, activation lease conversion and quality mutation reconcile |
| C047 | regression | Affected v0.6 stock/cash/quotes/event/bank/weight/contracts/Charter/trade checks pass; explicitly replaced damage/proximity/slots are not false regressions |
| C048 | network | Actual server+two independent clients at measured100/150 ms RTT and jitter; casts, swaps, source loss, claims and revisions authoritative |
| C049 | native | Cold build/cook/package and assets;1280×720/1920×1080,150 percent scale/125 percent text; cue/rejection/readability verified |
| C050 | privacy | Restricted client payload, target handles, minimap/audio/VFX/logs contain no unauthorized opponent sheet/identity/hidden-target coordinates |
| C051 | lifecycle | Ten restarts plus normal session shows no orphan collider/ward/gate/effect/lease/reservation/receipt growth; record hardware/frame/query costs |
| C052 | tactical | Level 8controller vs14claimant isolated scenario: meaningful interrupt without guaranteed duel win; claimant can counter by bait/sight/position |
| C053 | human | Two three-item loadouts produce different counter choices; observers can explain silence/mute/stun, counters, gear caps and why wealth is not victory |
| C054 | routes | Four separate normal-route end-to-end demos without grants, labelled times/resources/receipts; isolated fixtures kept separate |
| C055 | pacing | Normal35-minute1- and4-contender runs where available record level spread, first book/gear/route times, deaths/downtime, controls/claims and supply use |

## Tactical scenarios

1. Underleveled denial: level 8 Seeker vs level 14 geared claimant; ward/scout, smoke/detection choice, Force Wave/Counterpulse cancels claim. Record resources spent, response window, success/failure. Repeat with claimant clearing ward, baiting interrupt, bracing push or approaching flank. This tests counterplay, not mandatory underdog victory.
2. Silence/mute/stun: own Purge Touch and Null Bell; select the available response under each. For stun require external consenting Remedy or preapplied control guard. Record whether user understood why a self-cleanse could not fire.
3. Merchant interception: normal contract/cargo route with Harness/Satchel and two utility slots. Opponent threatens cargo, merchant can flee/recall ordinary cargo, bank earlier or trade time for safer route. Keys/Heartbreaker cannot escape via recall/gate. Record actual net profit and loss, not gross reward.
4. Build commitments: learn but cannot equip high-circle book; invest natural attributes to qualify, change at station, sacrifice one utility slot; Scholar's Sigil improves only allowed fields. Compare two three-item loadouts against same threat.
5. Rival claim: nonpurchaser wins public Charter; key owner starts overlapping personal attempt; tie/death/route close produce exactly one valid result. A bystander within240 cm cannot stall either without acting.

## Human review and telemetry

Observe actual humans where available; never invent participation or treat a scripted bot as enjoyment evidence. Record observed actions, confusion, time/assistance, input mapping, perceived counter, loadout opportunity cost, route time and recovery after death. Engineering fixtures isolate hard cases; normal no-grant runs establish progression feasibility; neither proves60–90 minute balance.

Collect combat disabled time (longest continuous stun/hex), MP/counter expenditure, fight commitment→death/escape, item-family locks, cheap-counter use, claim start/cancel reason and source, attempted/reached route, XP attribution, gold mint/burn/transfers, spare/equipped cargo drops, market stockouts and winner path. Benchmark performance on actual hardware against adopted baseline budgets, report unsupported packaging/server targets honestly.

A document-level checker pass means reference arithmetic/schema consistency only. This documentation task does not supply Unreal source, run engine tests, or certify game outcomes.
