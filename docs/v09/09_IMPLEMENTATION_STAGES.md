# Implementation stages — B90 to B94

Dependencies are strictly B90→B91→B92→B93→B94. A feature is complete only when its exit evidence exists in the native build. Keep actual source commits and test artifacts in the feature ledger. Do not mark a stage done because its actors import, markers appear, or a validator passes.

| Stage | Deliverable | Required exit evidence |
| --- | --- | --- |
| **B90** | Full terrain/road/river/settlement skeleton and spatial importer | All96 POIs and24 spawn pads load with stable IDs; permanent routes, ramps and bridge/floor continuity; native path/travel table and at least two Heart approaches |
| **B91** | Sight, interiors, mutable foliage and object interactions |36/144 enterable;72 doors;480 props;16 traversal anchors; fitted collision/vision/nav agreement; world/item action separation and race tests |
| **B92** | Service economy and living population | Five market pools, shared official stock, finite sources/contracts,196 noncontender-role capacity plus24 contenders; working routines/local security; no duplication/hidden-information AI |
| **B93** | Victory geography and four real routes | Three towers/unique keys/one boss/forge; every route earns access legally; ten-second contest, warnings, drops and endpoint tests in capital |
| **B94** | Full-session and multiplayer validation |60/75/90-minute native scenarios, restricted clients, worst-density stress and human pacing observations with recorded hardware/build/latency |

## B90 — world skeleton

Audit baseline first. Build manifest importer and deterministic native builder; fit terrain bands, ramp networks, river banks/floors, bridge decks and capital wet inset. Place all settlement blocks and POIs before detail. Preserve editable/final fitted transforms and IDs. Implement debug views for route graph, pad bounds, terrain bands, visibility proxies and native navigation. Run routes without any movement ability and with no object mutation. Record native reachability for every POI and two routes to key targets. Zero orphan markers.

## B91 — functional environment

Hook object types into inventory/interaction authority. Deliver one complete reference encounter (Riverport public frontage→rear door→warehouse crate→roof ladder→grass escape), then extend shared components across all instances. Implement static occlusion first, then door/prop/foliage changes with atomic proxy revisions. Add water/oil accessory uses, bounded torch ignition and existing Frost/Steam reactions. Show usable portable-item bindings, fuel and carry state. Include a native encounter in both Wet and dry Heart approaches. No cosmetic-only verbs.

## B92 — population and bounded economy

Initialize five exact market pools, common caches, resource charges and24 roster seats once. Add official counters and village work/recovery without duplicating stocks/rewards. Import152 routine assignments; implement all role capacities and ordinary AI contender legal commands. Audit inherited Witness and starter behavior. Add local witnessed security, muted/quiet behavior, explicit visual-only traveler cargo. Record NPC awake tiers and real accounting identities. Meet zero-kill progression and rival interference acceptance cases with actual receipts.

## B93 — route convergence

Place the three towers, rare ruin caches, single boss and Heart Foundry. Implement goal selection for contender AI without grants; use real acquired goods and access rights. Test a public Charter opening won by someone else; keys dropped/recovered with preserved lock; artifact consumed then owner dies; siege permanence; two simultaneous claimants; exact-window stun versus harmless surface slow. Confirm local guards/shrines/protection do not sterilize the precinct.

## B94 — full-match validation

Run24 locked contenders (human seats may be automated fixtures in soak runs, clearly labeled) at60,75 and90 min. An actual terminal winner is allowed to end a normal run early; long-duration stress fixtures may suppress final claim only in an isolated test mode with no balance claim. Require at least one60+ minute human-involved observed match before calling the full-session pacing validated. Do not equate automated soak with fun or balanced matches.

Targets: client60 FPS at a recorded resolution/quality/hardware; server30 Hz, p95 tick≤33.3 ms and p99≤50 ms; p95 acknowledged world interaction≤250 ms at documented RTT; high-level route-query p95≤10 ms. Stress the capital with24 contenders, local crowds, open doors, moving cargo, smoke/fields and max accepted field allocations; also stress distributed activity. Reject unsafe resource requests without charging; no dropped paid fields, hidden body leaks or claim timing drift. Targets are provisional and unmeasured.

Collect winner/route/time, first rival encounter, level/gold curves, work/camp quotas, first-visit/discovery XP, travel and idle intervals, regional heat, guard pursuits, valid/failed placements, visibility mismatches, late event queues, path failures, active AI tiers and conservation deltas. If most sessions end well before60 min or at NoAcquisition, report the failure and propose measured tuning; never insert an undocumented minimum-win gate or award a score winner.

## Completion report

Fill `templates/IMPLEMENTATION_REPORT.md` with source baseline, architecture reuse, per-stage status, native tests passed/failed/not run, manifests/hashes, screenshots/log paths, remaining defects and tuning deviations. Update `FEATURE_LEDGER.csv`. All v0.8 behavior remains regression scope unless this package explicitly overrides it. New map breadth is not a reason to remove established abilities/items or provide silent fake adapters.

