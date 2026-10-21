# Implementation plan

## B50 Audit and lock the sprint contract

Inspect delivered v0.4 source/assets/evidence, run a packaged baseline smoke check and record actual tuning and item supplies. Fill the ledger for retained versus changed behavior. Confirm mapping conflicts, private-view access and current inventory representation. Keep the engine and ability architecture.

Gate: baseline revision, actual deviations, targeted prerequisite repairs and the adopted proposed defaults are documented. No assumed missing v0.4 implementation.

## B51 Prove right-click navigation remotely

Implement order identity, native path planning/following, cancellation, approach snapshots, sprint and camera controls. Prove owner/server movement integration before building the full screen layer. Handle ramps, unreachable routes, stale order responses, focus loss, dash, death and new life.

Gate: two independent native clients can each route, supersede and stop their own body without teleports, double integration, hidden-target tracking or restored stale orders. Record prediction/correction behavior and selected native adapter. Depends on B50.

## B52 Centralize input and action modes

Add mapping registry, Q/W/E/R, item 1–4, F holds, attack mode, targeting, cancellation, panel routing, rebinding and saved local preferences. Remove old normal-profile bypasses. Expose timing/cursor states from the existing activation path.

Gate: no UI gesture falls through, no selected-other command is possible, no duplicate conflicting action fires, held channels work under packet delay and old shortcuts cannot bypass slots. Depends on B51.

## B53 Build the HUD inspector and minimap

Create own status/commands, selected-subject inspector, task/objective/life states, three-state fog minimap and source legend from authorized read models. Invalidate selection on sight loss. Integrate frontend/lobby/results into the same visual language.

Gate: actual remote payloads and widgets contain no extra opponent stats or hidden positions; self commands remain usable while inspecting. Map pan does not expand reach. Depends on B52.

## B54 Add the carried container and quick references

Create 24-slot inventory, unique finite charge records, filters/search/sort, revisioned organization, item details, four quick references, read/place/drop UI and accessible alternatives to drag. Preserve actual supplies and death/learning rules. Apply clean-match migration.

Gate: sorting preserves references, transfer/consumption clears them, books teach rather than cast, ward creation consumes once and a full bag never loses gold/items. Depends on B53.

## B55 Finish vendor purchase

Add the Guild catalog and own-bag view, live quote/version, pending purchase, acknowledgement and failure states. Reuse real item creation and 40-gold default where unchanged. Close correctly for lost eligibility.

Gate: normal earned money buys one actual book, capacity/stale quote/replay cases conserve ownership and UI never displays a nonexistent successful purchase. Depends on B54.

## B56 Implement consensual peer exchange

Implement invitation, limits/timeouts, role-neutral availability, six-item trays, physical reservations, unescrowed gold, versioned edits, Ready, separate final Confirm, cancellation and one atomic transfer. Add concurrent theft/death/use tests.

Gate: real participants exchange books/charges/gold or a Seal without private-bag leakage, one-sided transfer, stale consent or a gold-protection exploit. Depends on B55.

## B57 Apply consistent presentation and accessibility

Unify reusable widgets, language, states, icons and mapping labels. Reflow bottom HUD and panels across declared resolutions/text scales. Add keyboard panel navigation and button alternatives. Compare native screenshots with annotated diagrams.

Gate: critical status, own commands, both offer directions, Confirm/Cancel and focus are legible and reachable. No cosmetic screen hides incomplete behavior. Depends on B56; widget consistency can begin earlier.

## B58 Verify deliver and observe users

Run U01–U56 plus affected retained v0.4 checks. Cold build/cook/package, independent remote clients, input delay, privacy, inventory conservation, restart and normal peaceful progression must have real evidence. Observe uncoached tasks and record actual failures/time/assistance. Preserve existing valid unrelated results.

Gate: runnable native source/assets, synchronized data, completed ledger/report, normal-rule demo, screenshots, known limits and evidence. If tools or humans are absent, finish feasible engineering and label remaining work accurately. Depends on B57.

## Completion boundary

A polished mockup alone does not deliver v0.5. Nor does a working movement demo with unusable inventory. Review the complete journey: join → navigate → inspect → prepare → buy/read → bind/use → exchange → earn/claim. Expand content only after the shared UI and command rules are understandable and reliable.
