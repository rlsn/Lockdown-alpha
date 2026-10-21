# Acceptance and user review

All checks below are planned, not reported passes. Record actual source/engine/config revision, profile, clients, hardware and evidence. Reuse valid baseline results where unaffected; rerun changed input, inventory, authority and acquisition paths. Fixtures prove only their named isolated case.

## Engineering checks

| ID | Required result |
| --- | --- |
| U01 Baseline | Actual delivered v0.4 revision, evidence, adopted defaults and deviations are recorded |
| U02 Defaults | Q/W/E/R skills, 1–4 items, F context, explicit attack and right-click navigation work with no duplicate legacy action |
| U03 Mapping | Rebind, same-context conflict rejection, reset, persistence and every displayed mapping label agree |
| U04 Layers | Text, modal, drag, panel, targeting, minimap and world consume each gesture once; drag release never falls through |
| U05 Terrain route | Independent remote client follows real walkable ground around walls/trees, with no teleport or duplicate movement authority |
| U06 Height and projection | Ramp traversal works, cliffs/roofs reject wrong-floor projection and partial paths do not claim arrival |
| U07 Supersede and stop | New order replaces old; S/focus cancellation epoch defeats delayed route acknowledgements and old-life commands |
| U08 Approach | Observed unit/station/item approach uses snapshot and correct stop range; no auto-chase/use/attack/loot occurs |
| U09 Obstruction | Two-second stall triggers one replan; second stall stops clearly without oscillation or infinite query loop |
| U10 Speed and Step | Derived walk/sprint and Step collision/timing stay correct under path following and do not bypass combat constraints |
| U11 Facing | Moving faces travel, hovering/selection does not rotate own body and explicit aim preserves melee/rear/skill rules |
| U12 Basic attack | A then world click invokes one retained melee action; ordinary left/right selection clicks never attack |
| U13 Targeted skills | Step/Spark/Ambush arm, confirm, cancel and recheck actual range/sight without automatic approach or unslotted casts |
| U14 Held actions | Patch, Pickpocket, checks, reading, work, preparation and Heart use one Begin/Hold/End path, release/stale cancel and no cooldown refund exploit |
| U15 Action interruption | Move/Stop affect permitted locomotion without free cancelling committed attacks; stationary channels stop navigation and are interruptible |
| U16 Focus | Focus loss stops navigation/holds/targeting/trade; liveness expiry covers lost cancellation; focus regain does not replay inputs |
| U17 Major panels | Opening stops movement and cancels interruptible work; world shortcuts/edge pan are blocked; warning and timer remain; closing requires fresh input |
| U18 Camera | Free pan, edge toggle, bounds, zoom, Space recenter and Home follow work without changing sight or aim |
| U19 Minimap controls | Left pan and right move use authored surface; map input never casts/selects a hidden unit or changes action reach |
| U20 Minimap data | Three fog states, static POIs, source legend and dynamic removals use filtered views without hidden path/actor payloads |
| U21 Inspection ownership | Other selection changes inspector only; own HP/skills/items stay labelled and all commands target own body |
| U22 Private fields | Residents and contenders share a mask; other HP, level, build, ranks, wallet and bag remain Unknown and absent from payloads |
| U23 Sight loss | Selection/outline/tooltip/armed target/approach clear when authorization is lost; no automatic hidden reacquisition |
| U24 Remote source | Ward/tower sight permits masked inspection but body sight/range still controls action and trade |
| U25 Resources | Own HP/XP/gold and life states are real; no fake MP or extra mana mechanics appear |
| U26 UI outcomes | Pending/ready/active/cooldown/blocked/result states and server progress agree; duplicate receipts show one outcome |
| U27 Bag capacity | 24 unique carried instances replace four-book cap; gold and learned/receipt state are outside the grid; Seal count remains one |
| U28 Inventory revisions | Reorder/search/filter/sort preserve identity and reject stale/unauthorized changes with no duplicate or lost item |
| U29 Quick references | Four distinct own eligible item references add no capacity; sorting preserves them and ownership loss clears them atomically |
| U30 Books | Inventory and quick use run actual three-second learning; duplicate knowledge retains book; learned skill still needs a skill slot |
| U31 Wards | Actual finite supplies migrate exactly; both entry points place one legal source per charge; failure/cancel consumes none |
| U32 Drop and pickup | Explicit drop/confirm and real capacity/range pickup preserve unique ownership; dragging outside cancels without dropping |
| U33 Death inventory | Actual adopted charge policy, retained book/Seal drops, quick-reference cleanup and corpse-independent item life work without fresh respawn supplies |
| U34 Vendor | Real earned gold buys exactly one current quoted book within range/capacity; stale price, replay, pending spam and failure conserve state |
| U35 Invitation | Explicit consent, proximity/quiet/observation, rate limits, generic unavailable response and one session per participant work |
| U36 Trade privacy | Only own bag/balance and voluntarily offered terms reach each client; no hidden role, other inventory or wallet dump |
| U37 Offer revisions | Every relevant edit or wallet mutation clears both Ready/Confirm; stale session/revision/digest cannot commit |
| U38 Consent stages | Both Ready followed by two separate final Confirms of the same exact offer are required; changed offer returns to Draft |
| U39 Item reservations | Offered instances cannot be used/dropped/double-offered; cancellation releases locks without ownership transfer |
| U40 Gold not shielded | Incoming theft uses current wallet despite an offer; approvals invalidate; gold is not an invulnerable escrow |
| U41 Final limits | Six items per side, nonnegative bounded integer gold, net bag capacity and final one-Seal limit are validated; empty exchange rejected |
| U42 Conservation | 100/20 wallets offering 40/5 end 65/55; full bag giving two/receiving one ends 23; no partial transfer on failure |
| U43 Concurrent commit | Trade versus death/theft/use/cancel/replay serializes once; death releases reservations before drops and a late cancel cannot undo a committed receipt |
| U44 Timeouts | Invite 30 seconds, inactivity 60, hard session 180 and cancellation on range/sight/damage/disconnect/end leave no reservations or hidden pending transfer |
| U45 Trade progression | Real exchanged book can be read, ward placed and Seal claimed without granting XP, extra craft allowance or resetting cooldown/knowledge |
| U46 Layout | Native 720p/1080p and declared OS/text scaling plus 1440p keep essential controls and both trade directions legible without clipping |
| U47 Access alternatives | Panel focus, Tab order, input fields, button alternatives to drag, Escape, contrast and non-colour state cues work |
| U48 Frontend and life | Host/Join/Ready/errors, own death/return/protection and terminal result use coherent panels and no old Migration demo HUD |
| U49 Native delivery | Cold build, asset bootstrap, cook, packaged Win64 and two independent remote-client launch steps work on the pinned toolchain |
| U50 Delay | Actual 100/150 ms RTT and up to 25 ms one-way jitter preserve orders, cancellation, held work, trade consent and filtered views |
| U51 Restart and lifetime | Ten restarts and a normal 25-minute session leave no stale order, selection, item handle, session, reservation or duplicated delegate |
| U52 Performance | Record UI/pathfinding cost, frame times, order/query rate and actual traffic against the audited baseline; no unexplained persistent leak or major regression |
| U53 Peaceful regression | Normal zero-kill work→Witness→fabrication→Heart still works through new UI with no seeded gold/Seal/XP |
| U54 Character regression | Allocation/ranks/subclasses/books/equipment keep real costs and station rules; four shared skill slots remain authoritative |
| U55 Uncoached tasks | Real users attempt the task list; assistance/time/error counts and pending evidence are recorded without invented participants |
| U56 Honest handoff | Ledger/report distinguish real behavior, fixtures, retained evidence, failures, deviations and deferred long-session/conquest/accessibility work |

## Test methods and boundaries

Use focused helper checks for mapping conflicts, net exchange arithmetic and projected capacity; authority integration for identity, race and cancellation; native clients for routing, widgets and private views. An image comparison cannot prove absence of private fields. Inspect restricted client payloads/proxies/tooltips plus the independent server record.

Test exact ranges/timer boundaries, maximum gold, negative/overflow input, source loss on completion, stale life/revision, out-of-order acknowledgements and duplicate event listeners. Permute trade, death, pickup, ward use and theft in the same authority batch. A vanished item must never remain usable by quick reference. A dropped Seal claimed by another must still produce one winner.

No normal demo relies on grants or shortcuts. Tests may inject item/funds/skill state for isolated cases, with fixture labels; use real earnings for vendor and peaceful-regression demonstrations. No mana, class reveal or extra free utility is introduced through UI convenience.

## Performance and network evidence

Record hardware, resolution, graphics/UI scale, warmup, server topology, clients, current crowd and pathing workload. Retain established v0.4 budgets if available; starting targets remain median 55 FPS or better and p95 frame time under 33 ms at 1080p, with p95 project authority-plus-view work under 10 ms per 50 ms interval. Measure UI and navigation query contribution separately. Log query counts during rapid clicking and blocked routes; ten clicks per second must coalesce rather than accumulating stale paths.

Compare input acknowledgement and native movement corrections at measured 100 and 150 ms RTT. Initial acknowledgement target remains p95 below 250 ms at 150 ms RTT; route arrival and responsive cancellation need separate observations, not just an RPC latency number. Record total native and filtered-view traffic; preserve the retained view target below 100 KiB/s per client on average unless a documented actual baseline differs.

Run a normal 25-minute session and ten restarts. The existing 90-minute technical soak need not be repeated without a lifetime concern introduced by this sprint; if used, label all overrides. Neither run establishes full 60–90 minute human pacing.

## Uncoached review tasks

Use at least two independently controlled human clients and at least three uncoached reviewers where available; run the core task set twice, first default then one changed binding/text setting. One person may be an observer rather than a simultaneous third client. Do not claim small samples establish universal usability.

1. Host/join, choose a class and explain what remains yours while inspecting another body.
2. Reach a town around an obstacle, sprint, stop and pan/recenter without issuing an accidental attack.
3. Attempt explicit attack/targeted skill and cancel targeting; start and release a contextual hold.
4. Buy a book with earned gold, find/read it, explain learned versus equipped and prepare a valid mixed loadout.
5. Assign/use a finite ward through a shortcut and find it again after sorting; identify a full bag or blocked action.
6. Invite a trade, change an offer after Ready, complete one real exchange and cancel another without loss.
7. Explain Unknown opponent fields and why a distant public source does not enable remote interaction.
8. Survive an interruption/death/return and explain retained build, dropped items and cleared controls.

Record completion without assistance, time, wrong clicks/accidental commands, recovery, questions and subjective clarity per task. Proposed review gate: at least 80 percent of attempted core tasks completed without coaching on the second pass, with zero ownership/currency loss, involuntary trade or private-data exposure. Report raw counts and unresolved critical failures; a threshold does not excuse such failures. If people are unavailable, mark human review pending and finish feasible engineering without inventing results.
