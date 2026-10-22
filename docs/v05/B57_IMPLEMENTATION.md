# B57 presentation and panel accessibility

22 October 2026. Predecessor: `6b71c80` (B56). Unreal 5.8.4 CL 58902052.
This is the engineering presentation checkpoint. B58 release verification,
the declared Windows 150% scale matrix and actual user review remain open.

## Shared interface

`LDWidgets` now supplies the common dark panel, text, command button and scrolling
offer tray construction. The inventory, vendor and trade continue using one real
bag; Character and Controls use the same shell. Skill/item availability stays
textual, with live remapped keys, numerical cooldowns/holds and explanations.
Skills and Items have separate labelled rows and different slot borders.
Books are labelled as books; learning, inventory and targeting remain separate.
Player feedback uses Pending, Catalog updated and Trade cancelled. Invitations
no longer display a numeric appearance code. Protocol fields, authority checks,
reservation rules, exact displayed offer digest/revision and receipt ownership
remain in place.

`ULDPanel` is a shared keyboard focus scope for bag/vendor/trade, Character and
Controls. Tab/Shift-Tab traverse visible enabled buttons and text fields in tree
reading order, wrap within the active scope and reveal scrolled controls. A gold
focus outline is painted above the current button or field. Enter/Space activate
the focused panel button once; repeat events do not repeat the action. Text fields
retain normal typing/commit behavior. World command buttons stay unfocusable.
This does not implement full keyboard world play or screen-reader support.

The drop confirmation and final trade review restrict traversal to their own
controls and disable the underlying bag. Escape dismisses the sublayer before
closing the major panel. Inventory handles Escape before an editable field can
consume it, and typing the inventory binding in search/gold fields does not close
the panel. Existing major-panel switches still close/cancel the previous layer.

## Layout and final trade review

The lower HUD uses a 220-logical-unit band at 1080p with normal text, and 250 for
125% text. This is a deliberate larger-text deviation from the proposed 220 px;
it keeps hold/cancel status visible. At 720p the band is 180 screen pixels, computed
from the measured viewport scale rather than treating logical units as pixels.
Own skills and item references reflow to two compact rows with one-line slots.
The inspector scrolls inside its allocation, preventing text from covering its
buttons. Its compact Build/Bag mapping labels remain live. The map legend wraps
inside a bounded column. HUD command targets have a 40-logical-unit minimum.

The Character title and Close/Back control stay fixed while its preview/library
content scrolls. The bag continues to use four columns at 720p and six at wider
sizes, with independent bag/detail scrolling. Newly rebuilt bag cells inherit the
current text preference. Body copy and controls respond to the 125% preference;
long bag labels wrap inside a specified width. Button captions use explicit line
breaks or a single line, avoiding deferred auto-wrap text outside button bounds.

Both Ready approvals automatically open a separate final-review layer. It shows
the current offer number, outgoing/incoming totals, both real offer lists and
confirmation states. Confirm exchange, Back to offer and Cancel trade are fixed
below the scrolling trays. Confirm uses the already displayed snapshot and remains
a separate gesture; Back/Escape do not imply consent. The cache distinguishes session, offer number and review/receipt stage; a new
session cannot inherit an earlier session's dismissed review. An edited/invalidated
offer closes the review and restores panel focus. Reviewing remains available
after your own confirmation while waiting for the other player. A completed exchange shows its actual
receipt with a Close action. No world pause or protected gold is introduced.
The clock, own HP/life and incoming warning layer remain above every major panel.

## Native verification

Final Editor build: `20261022-215604-363-BuildEditor`; Game target:
`20261022-215914-470-BuildGame`, both with four compile actions maximum.
Asset validation: `20261022-220110-626-Bootstrap-Validate`, 28 packages,
zero errors and the four retained CrowdFollowing/Recast warnings. Cook/package
and final cooked runtime results are recorded below.

The native final bag capture `20261022-215726-511-InputUI-1280x720-Inventory-125pct`
passes seven checks: focus scope, visible target measurements/offscreen desired
size, every Tab/Shift-Tab target, the inventory binding in search, focused-field
Escape, and screenshot output. `20261022-215753-873-Trade-100-25-Death-UI1280x720-125pct`
passes server 43 / A 17 / B 12, including actual Space/Enter/Escape routing,
modal focus isolation and gold digits without quick-item activation. Its measured
RTT is 123.72 / 123.43 ms with base 29 and jitter 25; this is not an exact 100 ms
pass. The earlier base-53 native run measured 169.96 / 170.51 ms and passed 43 /
16 / 12 before the final Escape/session refinements. Final cooked delay evidence
supersedes those native target settings below.

The retained rule run `20261022-215436-050-Automation` passes all 19 tests,
including the named `TradePrivacy` reflected-field allowlists. Final cooked
checks and the repeated final rule run follow below.

Visible targets are checked using cached native geometry. Fully unpainted scrolled
controls have no cached geometry yet; their desired sizes are checked separately.
This distinction is explicit in the report text rather than treating zero cached
geometry as a measured tiny button.

## Cooked package and screenshots

Final warm Development archive:
`Artifacts/20261022-220153-009-PackageWin64/Archive/Windows`.
UAT build/cook/stage/archive succeeds; cook reports zero errors and zero warnings.
`PackageManifest.json` records the precommit parent and dirty source snapshot.
`Artifacts/VerifyB57Package.ps1` verifies all 145 Source/Config files and seven
runtime payload hashes; `Artifacts/B57PackageProvenance.json` binds that exact
snapshot to the final commit. Source/Config stayed frozen through cooked probes.
This remains a warm package; B58 cold-checkout evidence is separate.

All 14 following native cooked screenshots were opened and visually inspected,
including all four corners, focus outlines, fixed actions and scrolling content.
The primary-monitor query is 100% for every run. Scale below is actual effective
UMG / text preference; it is not OS scaling. Each named artifact folder is under
`Artifacts/` and contains the scene PNG, environment, native report and log.

| Scene | Actual viewport | UMG / text | Checks | Artifact folder |
| --- | --- | --- | --- | --- |
| Selected | 1280×720 | 0.666 / 1.00 | 2 | `20261022-221658-380-Packaged-InputUI-1280x720-Selected` |
| QuickItems | 1280×720 | 0.666 / 1.25 | 2 | `20261022-221710-452-Packaged-InputUI-1280x720-QuickItems-125pct` |
| DropConfirm | 1280×720 | 0.666 / 1.25 | 5 | `20261022-221719-704-Packaged-InputUI-1280x720-DropConfirm-125pct` |
| InventoryDead | 1280×720 | 0.666 / 1.25 | 5 | `20261022-221728-936-Packaged-InputUI-1280x720-InventoryDead-125pct` |
| VendorFull | 1280×720 | 0.666 / 1.25 | 6 | `20261022-221738-203-Packaged-InputUI-1280x720-VendorFull-125pct` |
| Development | 1280×720 | 0.666 / 1.25 | 5 | `20261022-221748-704-Packaged-InputUI-1280x720-Development-125pct` |
| Controls | 1280×720 | 0.666 / 1.25 | 5 | `20261022-221757-958-Packaged-InputUI-1280x720-Controls-125pct` |
| Death | 1280×720 | 0.666 / 1.25 | 2 | `20261022-221807-138-Packaged-InputUI-1280x720-Death-125pct` |
| Result | 1280×720 | 0.666 / 1.25 | 1 | `20261022-221816-339-Packaged-InputUI-1280x720-Result-125pct` |
| Selected | 1920×1080 | 1.000 / 1.00 | 2 | `20261022-221826-606-Packaged-InputUI-1920x1080-Selected` |
| QuickItems | 1920×1080 | 1.000 / 1.25 | 2 | `20261022-221835-806-Packaged-InputUI-1920x1080-QuickItems-125pct` |
| HUD | 2560×1440 | 1.333 / 1.00 | 2 | `20261022-221845-020-Packaged-InputUI-2560x1440-HUD` |
| Inventory | 2560×1440 | 1.333 / 1.00 | 7 | `20261022-221854-221-Packaged-InputUI-2560x1440-Inventory` |
| Controls | 2560×1440 | 1.333 / 1.00 | 5 | `20261022-221903-523-Packaged-InputUI-2560x1440-Controls` |

Death/blocked, empty, full, selected stranger, bag, vendor, drop confirmation,
settings, Character and terminal states are represented. Delayed peer review,
completion and six-item offers follow below. A full hover/drag/pointer calibration
and physical keyboard state sweep remains part of B58 on-screen/user review.
Gold text injection uses native Slate character events; physical key press routing
is not inferred solely from those events. The compact dead-overlay clock cue can
wrap to a third line beyond its background at 125%; its text remains readable.
This minor background sizing issue remains recorded for B58 presentation review.

## Cooked authority, input and delay regressions

All reports use independent authority and two remote clients. The packaged
headless listen authority has a nonparticipating local connection; this does not
certify a source-engine dedicated-server target. Trade participants retain the
B56 labelled wallet/bag/position/theft/deadline fixtures and real transaction rules.

| Trade ending | Injected one-way range, ms | Checks server / A / B | Actual echo RTT A / B, ms | Artifact folder under Artifacts |
| --- | --- | --- | --- | --- |
| Actual due lethal melee/death | 28–78 | 43 / 17 / 12 | 166.02 / 149.49 | `20261022-221912-733-Packaged-Trade-150-25-Death-UI1280x720-125pct` |
| Actual No acquisition deadline | 4–54 | 42 / 17 / 12 | 119.55 / 122.56 | `20261022-222017-784-Packaged-Trade-100-25-Terminal-UI1920x1080` |
| Actual remote disconnect | 28–78 | 41 / 17 / 11 | 154.47 / 169.35 | `20261022-222123-290-Packaged-Trade-150-25-Disconnect-UI2560x1440` |
| Actual No acquisition deadline | 0–37 | 42 / 17 / 12 | 66.18 / 66.18 | `20261022-222936-398-Packaged-Trade-100-25-Terminal-UI1920x1080` |
| Actual No acquisition deadline | 0–50 | 42 / 17 / 12 | 82.82 / 82.68 | `20261022-223317-890-Packaged-Trade-100-25-Terminal-UI1920x1080` |
| Actual No acquisition deadline | 33–33 | 42 / 17 / 12 | 82.87 / 82.79 | `20261022-223725-169-Packaged-Trade-100-0-Terminal-UI1920x1080` |
| Actual No acquisition deadline | 25–45 | 42 / 17 / 12 | 99.49 / 99.40 | `20261022-224004-247-Packaged-Trade-100-10-Terminal-UI1920x1080` |

Each trade capture contains Review, Completed and SixItems PNGs. All nine images
from the first three runs, plus the three final near-100 ms captures, were opened
and inspected at their native resolution
(the viewer may downsample 1440p). Both directions and the final summary remain
readable, Confirm/Cancel are fixed, and completed receipts have a fixed Close
control. Draft trays may scroll to reach all six offers; their item counts remain
in the summary. Rendered A viewports/scales are 1280×720 / 0.666 / text 1.25,
1920×1080 / 1.000 / text 1.00, and 2560×1440 / 1.333 / text 1.00. Headless B
reports a zero viewport; its scale is not UI evidence.

Initial packet settings were base 53±25 ms per direction for the nominal 150 ms
runs and base 29±25 for nominal 100 ms. Measured values above include frame/tick
scheduling; the latter initially measured about 120 ms, so it is retained as
intermediate-delay evidence rather than labelled an actual 100 ms result.
Additional base/jitter settings 12/25, 25/25 and 33/0 produced approximately 66,
83 and 83 ms respectively; these are retained calibration results, not 100 ms
passes. The final base 35 / jitter 10 run (25–45 ms injected each direction)
measured **99.49 / 99.40 ms** and passes 42/17/12. This complements the nominal
150 ms lanes (actual 149–169 ms with up to 25 ms one-way jitter). Scheduling
and uniform-delay settings are reported separately from observed transport.

| Regression | Checks server / A / B | Actual RTT A / B, ms | Artifact folder under Artifacts |
| --- | --- | --- | --- |
| Vendor | 23 / 19 / 14 | 149.67 / 149.67 | `20261022-222228-388-Packaged-Vendor-150-25` |
| Inventory | 59 / 25 / 25 | 149.47 / 149.52 | `20261022-222517-641-Packaged-Inventory-150-25` |
| HUD | 26 / 37 / 37 | 132.99 / 133.07 | `20261022-222622-313-Packaged-HUD-150-25` |

Vendor still covers earned purchase/read, stale catalog review and conservation.
Inventory retains 24 slots, finite physical items, stable quick references,
death/drop behavior and bounded payloads. HUD retains real key/mouse route checks,
remapping, focus/fresh-input cancellation, masked other inspection, stale view,
return and terminal behavior. The HUD lane targeted 150 ms but measured about
133 ms; it is recorded as such. B56 unrelated navigation/region/privacy evidence
is retained, not relabelled as a new B57 run.

Final automation: `20261022-222738-262-Automation`, all 19 rule tests pass,
including `TradePrivacy`. All final cooked gates have zero failed checks.

## Comparison with the annotated wireframes

[HUD](wireframes/01_HUD_SELECTION.svg): Yours remains anchored, selected stranger
fields stay Unknown, the map distinguishes sight and fog, and numeric hold/cancel
feedback stays beside own commands. Compact slots trade rank text for a tooltip;
rank and detailed own skill values remain available there.

[Inventory](wireframes/02_INVENTORY_QUICK_ITEMS.svg): real carried items occupy bag
cells; the four item shortcuts remain references. Search/filter/sort, item details,
Use/Learn, Assign/Unassign, Move and confirmed Drop remain real actions. Content
scrolls while the quick-reference and Close strip stays fixed.

[Peer review](wireframes/03_PEER_TRADE_REVIEW.svg): the native confirmation sublayer
shows both voluntary directions and the final summary together, with independent
Ready/Confirm states and fixed Confirm/Cancel. The partner wallet and unoffered bag
never appear. The native review summarizes exact outgoing/incoming terms; a
separate projected-wallet arithmetic line from the drawing is not currently shown.
Authority still validates net gold, bag capacity and one-Seal limits. The drawings
are references, not imported images standing in for UI.

## Corrections retained in the evidence

The first build (`20261022-212258-769-BuildEditor`) caught a variable hiding the
inherited widget Slot member. Two later failed builds (`20261022-213802-354-BuildEditor`
and `20261022-214222-488-BuildEditor`) caught an edit script reusing a header buffer
for a source file; both source implementations were restored and rebuilt before
runtime acceptance. No failing build was packaged.

The initial rendered HUD exposed inspector/button overlap and a lower status row
outside the band. Inspector scrolling, bounded map copy, compact two-row commands
and the larger-text band corrected these; native geometry checks now cover bounds,
button overlap and minimum target dimensions. The 1080p larger-text failure is
retained in `20261022-214458-779-InputUI-1920x1080-Selected-125pct`.

The first new trade-key assertions in `20261022-213429-893-Trade-100-25-Death-UI1280x720-125pct`
used 255 for the unarmed skill sentinel; the controller actually uses -1. Actual
trade/server checks passed; corrected key assertions pass in subsequent runs.
The Character size diagnostic at `20261022-215054-119-InputUI-1280x720-Development-125pct`
identified two unpainted offscreen library controls (cached 0x0; desired 124x56 and
85x56), leading to the explicit visible/desired measurement distinction above.
Traversal captures also reset scroll offsets before screenshots so the traversal
exercise does not leave the panel scrolled to its last control.

## Boundaries and remaining acceptance

The capture scripts record actual viewport size, effective UMG scale, text scale
and a read-only primary-monitor Windows scale query. The present desktop reports
100%. Offscreen native screenshots do not certify window placement, pointer
calibration or interaction at another OS scale. 720p/1080p Windows 150% and the
complete on-screen matrix still require B58 evidence; they are not inferred from
UMG scale or the text preference. Ultra-wide, 4K, controller layouts, screen-reader
integration and full keyboard world play remain outside the declared scope.

Native probes exercise actual Slate input and real game/read-model states, with
labelled fixture grants and teleports. They are not uncoached user observations,
normal earned progression, physical WAN or performance acceptance. B58 still owes
cold checkout/build/cook, affected restarts, normal v0.5 lifetime/journey, release
report and human tasks. The user-deferred 90-minute stress run is not resumed.
