# B55 quoted Guild vendor

22 October 2026. Predecessor: `a6476b4` (B54). Native Unreal 5.8.4 CL 58902052,
CharacterMovement, Enhanced Input, UMG/Slate and the existing authority remain.
This checkpoint implements the Guild vendor; B56–B58 still complete v0.5.

## Quoted authority path

The V05 Guild sells the retained Step, Spark, Patch and Parley unread books,
one unique instance per explicit Buy, unlimited public stock, forty gold by default.
`LDVendorDirector` owns sessions, live catalog revision/price, bounded requests and
receipts. Its configuration is separate from unchanged v0.4 tuning. No Sell,
resale, new equipment, extra supplies, fees, trade XP or peer exchange is added.

Opening requires an active unprotected body within 120 cm of the Guild, clear
body ground and sight, a running match and no occupied action. It uses a fresh
owner-scoped session and open token. The owner receives public offer definitions
and prices, catalog revision and its own session identity; existing own snapshots
supply its own bag/balance. No stranger's inventory, wallet, role or canonical
vendor/item actor is replicated. The eight observed fields and 22 public fields
are unchanged. The vendor actor is server-only.

Buy carries match/life, request ID, own session, expected catalog revision/price
and expected inventory revision. Authority rechecks all of them and current
eligibility, capacity and funds. It creates/adopts one physical B54 book before
one debit, then returns the committed owner snapshot, item and outcome. Failures
change neither wallet nor bag. A bounded receipt cache returns the completed
outcome and current own state on replay; a replay still pending waits for its
final receipt. Request rate and queue bounds prevent unbounded accumulation.
The old unquoted inventory Buy and development Buy cannot bypass this path in V05;
explicit V04 retains its historical purchase route.

Catalog updates refresh the live quote. A stale submitted quote is rejected rather
than charged at a different price. Price updates also switch the button to Review
new price: the first click acknowledges the displayed update without sending a
purchase, and a subsequent explicit Buy submits it. This prevents a click on a
previously rendered price from immediately accepting an arriving changed quote.
There is no automatic purchase retry. Already learned books are labelled and can
still be bought intentionally; they retain the B54 duplicate-reading rejection.

Due combat/death is finalized first, followed by inventory, vendor, development
and objective processing. A vendor session is invalidated before its queued
purchases when range, body sight, protection, occupied action, life or match state
changes. A real positive damage timestamp also closes it, even if HP later heals.
Close tokens prevent an in-flight quote from reopening a dismissed panel. An
already committed purchase remains committed after closure; closure cannot undo
its receipt or duplicate its item. Disconnect/world cleanup removes the session;
no reservation, wallet escrow or cross-match vendor persistence is introduced.

## Native service and shared bag

Guild books is a visible contextual HUD service near the Guild, a bag toolbar
button, and a link from character/preparation. The V05 character panel no longer
has four direct unquoted Buy buttons. Opening uses the existing own bag shell,
stops movement/releases holds, suppresses world commands and leaves the match
running. The public catalog sits above the own inventory and item details.

The panel shows definition, known/not-learned state, taught rank, actual reading
and quiet durations, no auto-equip/cast, stock, price, own capacity/gold and current
outcome. Pending purchase disables repeated submission. There are no optimistic
books or balances. Capacity/insufficient funds, refreshed-price review, successful
receipt and creation failure use actual owner/authority state. Existing bag
organization, explicit drop and Use remain available when legal; Use exits the
vendor before arming the fresh B54 read/ward gesture. Focus or panel closure
invalidates its quote; loss of service eligibility closes the actual major panel.

## Verification

Evidence paths are relative to repository-root `Artifacts/`.

| Check | Actual result | Evidence |
| --- | --- | --- |
| First native vendor lane | Server 21 / A 18 passed; B had one fixture assertion failure | `20261022-150842-289-Vendor-150-25` |
| Editor build | Passed | `20261022-151424-435-BuildEditor` |
| Native vendor, target 150 / jitter 25 / base 53 | Server 23 / A 19 / B 14; actual echo 170.55 / 169.87 ms | `20261022-151634-702-Vendor-150-25` |
| Native inventory regression | Server 59 / A 25 / B 25; actual echo 169.87 / 169.42 ms | `20261022-152138-885-Inventory-150-25` |
| Rules | 18 passed, zero test warnings | `20261022-152256-446-Automation/Report/index.json` |
| Variable-frame combat | 48 passed | `20261022-152356-477-CombatCheck-VariableFrames` |
| Retained explicit V04 region | 93 passed | `20261022-152414-027-Region-DevelopmentCheck` |
| Game build | Passed, MaxParallelActions 4 | `20261022-152428-936-BuildGame` |
| Asset validation | Existing 28 packages validated without regeneration | `20261022-152635-139-Bootstrap-Validate` |
| Win64 Development cook/stage/archive | Passed; cook zero errors/warnings | `20261022-152659-513-PackageWin64` |
| Cooked vendor, target 100 / jitter 25 / base 29 | Server 23 / A 19 / B 14; actual echo 108.24 / 108.23 ms | `20261022-152813-345-Packaged-Vendor-100-25` |
| Cooked vendor, target 150 / jitter 25 / base 53 | Server 23 / A 19 / B 14; actual echo 149.59 / 149.56 ms | `20261022-153103-562-Packaged-Vendor-150-25` |
| Cooked inventory, target 150 / jitter 25 / base 53 | Server 59 / A 25 / B 25; actual echo 153.32 / 152.14 ms; full-drop view 4248 bytes | `20261022-153355-378-Packaged-Inventory-150-25` |
| Cooked HUD/input, target 150 / jitter 25 / base 49 | Server 26 / A 37 / B 37; actual echo 153.89 / 152.94 ms | `20261022-153500-550-Packaged-HUD-150-25` |
| Cooked restricted-view privacy | Server 12 / A 29 / B 27; maximum serialized client views 876 / 775 bytes | `20261022-153615-853-Packaged-ViewNetworkCheck` |
| Cooked 720p catalog | Captured and visually inspected | `20261022-153633-775-Packaged-InputUI-1280x720-Vendor/Vendor.png` |
| Cooked 1080p catalog | Captured and visually inspected | `20261022-153644-381-Packaged-InputUI-1920x1080-Vendor/Vendor.png` |
| Cooked 720p full bag, 125% text | Captured and visually inspected; capacity explanation and disabled Buy visible | `20261022-153653-795-Packaged-InputUI-1280x720-VendorFull-125pct/VendorFull.png` |
| Cooked 720p stale quote, 125% text | Captured and visually inspected; Review price, actual rejection and Close visible | `20261022-153703-089-Packaged-InputUI-1280x720-VendorFailed-125pct/VendorFailed.png` |

The earlier Editor-rendered 720p checks were also visually inspected:
`20261022-151939-612-InputUI-1280x720-Vendor-125pct/Vendor.png`,
`20261022-152051-245-InputUI-1280x720-VendorFull-125pct/VendorFull.png` and
`20261022-152115-175-InputUI-1280x720-VendorFailed/VendorFailed.png`.
At 720p the own bag and details/actions scroll independently while price review,
capacity and footer Close remain visible. At 1080p all 24 cells fit. These captures
are the native implementation, with labelled own-state layout fixtures; complete
keyboard/focus/physical-pointer and OS-scale acceptance remains B57–B58.

The asset-validation commandlet retains its existing CrowdFollowing/Recast
warnings while opening maps; it still reports bootstrap success for 28 packages.
The subsequent cook reports zero warnings. Native/cooked traversal is tested
through the actual courier route rather than inferred from validation output.

The current warm archive is
`Artifacts/20261022-152659-513-PackageWin64/Archive/Windows`. Its
`PackageManifest.json` records the B54 parent revision with sourceDirty=true,
because the B55 source was intentionally validated before its commit. The
137 source/configuration hashes and seven executable/container/registry hashes
match the final source and archive. `Artifacts/B55PackageProvenance.json` binds
the post-commit audit to the implementation commit; no Source/Config edits follow
this cook. Historical archives retain their original provenance.

Reproduce with `BuildEditor.ps1 -MaxParallelActions 4`, `RunTests.ps1`,
`RunCombatCheck.ps1 -VariableFrames`, `RunDevelopmentCheck.ps1 -Region`,
`BuildGame.ps1 -MaxParallelActions 4` and `PackageWin64.ps1`. Use that archive as
PackageRoot for `RunVendorCheck.ps1 -Region -RoundTripMs 100 -JitterMs 25
-OneWayBaseMs 29` and the 150 target with base 53, then inventory/HUD/privacy
regressions. Scripts run authority and clients sequentially relative to other
Unreal lanes. Named targets describe intended conditions; each JSON records
actual median echo and each Environment.json records injected settings. These
are local independent clients, not a physical WAN or human network session. The
vendor runner defaults to the saved regional map; requesting its unsupported
nonregional mode fails before launching Unreal.

The vendor probe uses separate authority and two independent clients. A begins
with zero gold and no paid jobs, uses native navigation and actual context holds
to complete two ordinary courier contracts, spends forty of its earned fifty gold
on one quoted book, replays the receipt and completes a real three-second read.
During this ordinary segment the harness grants no gold/books/XP, changes no
protection, teleports no body and retains ordinary prices, rewards, movement and
timers. Ordinary courier and first-visit awards remain active. Automatic native input is
engineering evidence, not human task completion or a full peaceful Heart journey.

After that ordinary proof, separate adversarial phases explicitly set eighty-gold
wallets, fill bags, change a catalog price to 41 and back, inject one creation
failure, move bodies to test range/combat, and terminate a life/match. They check
stale quote/bag/session, intentional known-book purchase, capacity, replay, pending
spam and failure conservation, late quote closure, actual positive hit/occupied
action, death and terminal closure. The retired direct Buy is attempted with a
free slot. These fixtures do not establish earned progression or economic balance.
Rendered vendor captures seed own books/gold/knowledge for layout and show actual
live quotes or actual stale-quote rejection. No human or OS scaling is fabricated.

## Failures corrected

The first compile found an unescaped multiline text constant and a unity-build
name collision between an existing soak fixture's local Accepted and navigation's
constant. The local variable was renamed without changing the soak behavior; the
user-deferred stress run was not resumed. The next compile caught one remaining
reference to that renamed variable, which was corrected. Both failed build logs
remain at `20261022-145745-598-BuildEditor` and
`20261022-150535-854-BuildEditor`; the final Editor and Game builds above passed.

The first independent vendor run passed its server checks, ordinary earned
purchase/read and all other adversarial behavior, but B's known-knowledge assertion
selected Patch for a Scholar that actually starts with Spark/Analyze. The fixture
now selects the actual known Spark definition. The price-update Review step and
retired-buy checks are included in the final lane rather than counted as part of
that earlier run.

## Review boundary

B56 consensual atomic peer exchange, B57 complete shared presentation/focus/OS
scaling and B58 cold checkout, ten restarts, normal v0.5 25-minute UI journey,
performance and uncoached human review remain. Current builds/cooks are local and
warm. The user-deferred 90-minute stress run remains deferred. Dedicated source-
engine packaging and remote Git/LFS delivery remain unverified/conditional.
