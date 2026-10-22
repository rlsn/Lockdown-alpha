# B54 carried inventory and quick references

22 October 2026. Predecessor: `b0af13e` (B53). Native Unreal 5.8.4 CL 58902052,
CharacterMovement, Enhanced Input, UMG/Slate and the existing authority remain.
This checkpoint implements carried inventory; B55–B58 still complete v0.5.

## Ownership and transactions

`ALDItem` is the common server-only physical record for books, ward charges and
the Concord Seal. It carries identity, kind/definition, container, slot, state,
availability revision and a reservation flag. Books/Seal retain their existing
actor subclasses and effects. The generic holder can represent an NPC; no random
NPC equipment, NPC bag disclosure or NPC inventory AI was added.

The V05 owner receives a revisioned `FLDInventory` with capacity, owned entries
and four identity references. Capacity comes from the new inventory config (24),
not the frozen v0.4 catalog's historical four-book limit. Gold, knowledge, points,
Witness and work credits remain outside the grid. One Seal counts as one slot
and still has a one-per-carrier limit. Books and charges have no extra type cap.
Legacy Books/WardCharges/SealId own fields are compatibility projections of the
V05 inventory. Explicit V04 keeps its retained transaction paths/capacity.

`LDInventoryDirector` validates match/life, request, inventory/item revisions,
ownership, locks, capacity and applicable body geometry before mutation. Requests
are bounded/rate-limited and sampled after combat/death, before the remaining
noncombat/objective work. A bounded receipt cache returns the latest outcome and
current own snapshot on replay. Read completion feeds the same receipt cache.
No optimistic ghost becomes an actual item or balance.

Sorting uses kind, definition name and stable identity. Reorder and binding change
the container revision, without changing skill build or item availability revision;
organization cannot cancel/complete a reading hold by itself. Moving a reference
clears its old slot atomically. Consumption, drop and ownership loss clear references
in the same mutation. Reserved items cannot bind, move, use or drop; full peer
reservation/session semantics are B56.

Pickup obtains a version quote for a currently observed, nearby dropped item. The
reply contains the audience-scoped observed handle and revisions, without canonical
item identity, position, owner or bag. Commit rechecks visibility, own-body sight,
80 cm range, life, capacity and versions. It cannot use remote-source sight as body
reach. Focus/panel/life cancellation discards the local pending pickup token.
The existing eight observed fields and 22 public fields remain the privacy boundary.

The bounded view envelope grows from 64 to 128 records: four full 24-slot death
drops alone require 96 entries. The existing byte count and per-record mask stay
unchanged. The worst-case serialization rule now requires the expanded envelope
to fit within 8 KiB, and the independent fixture tops up dropped world items to
96 after real death/return to check that legitimate items are not truncated.
These are world fixtures, not supplies granted to returned bags. Actual traffic
and full UI performance budgets remain separate B58 measurements.

## Real item paths

Starting a clean V05 match converts exactly the existing single finite ward charge
to one instance, once. Repeating initialization grants nothing. The old direct ward
RPC cannot bypass the instance path. A shortcut or bag Use arms the owned charge;
world confirmation places through the retained 96 cm/same-level/clear-ground rule.
Right-click/Escape cancels without cost. Source creation precedes atomic consume;
failure/replay cannot lose another charge or create another source. Existing two
active sources, 20 HP, 560 cm sight and 60-second lifetime remain.

A book shortcut holds its initiating key/button. Bag Use closes and arms a fresh
F learning hold. Both enter the retained stationary three-second read after six
quiet seconds, with 100 ms refresh/250 ms stale expiry. Early release keeps the
book; known knowledge rejects without consuming or starting a channel. Completion
consumes exactly that instance, teaches R1, clears its reference and does not equip,
cast or spend skill points. The old direct read RPC is blocked in V05. Preparation
retains its existing station, build revision and two-second hold.

Voluntary Drop is an explicit named modal confirmation. Authority requires the
current owned/unlocked instance, active unprotected body, no occupied use, and legal
ground/body sight within 80 cm. It never silently uses a Guild fallback. Mandatory
death drops preserve the retained book/Seal policy and legal-ground fallback;
unused carried charges are destroyed once, with no fresh return supplies. The
items have independent actors and survive body/corpse cleanup. Learning persists.
New-match level travel clears all match ownership and references; no save migration
or cross-match inventory persistence was added.

The retained 40-gold book purchase now checks common capacity before creating and
charging. This is capacity integration, not the quoted vendor UI/earned purchase
delivery required by B55. Trade remains unavailable until B56.

## Native interface

I/Bag opens the own 24-slot grid, six columns at 1080p and four at 720p, with scroll
when needed. Search and All/Books/Wards/Objective filters are local views. The panel
shows capacity/gold, selected definition/outcome, reserved/owned/shortcut state,
Sort, Use, Assign, Unassign, Move and Drop. Major-panel input stops/releases the
world and closing requires fresh input. Death leaves an inspectable organization
panel; actual gameplay item use remains unavailable.

Drag captures its item until release, moves among bag cells or binds an item
shortcut, and cancels outside without a world drop. Focus/life/panel cancellation
releases capture. Explicit buttons provide alternatives. Drop has its own modal
layer with Confirm/Cancel rather than a hidden world click. The HUD's four item
buttons use real references, live mapping labels (including active reading), Learn/ward wording and
Empty/Ready/Armed/Pending/Active/Blocked states. Own commands remain anchored while
another subject is inspected. The development panel links to the full bag and
labels its retained four-book preview accurately.

Static cells are created on capacity/column changes. Inventory contents refresh
on inventory/local-view changes; status uses own state. Widgets do not poll the
world or reconstruct all 24 entries per frame. B57 still owns full focus, Windows
scaling and presentation acceptance.

## Verification

Evidence paths below are relative to repository-root `Artifacts/`.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Final Editor including navigation-fixture isolation | Passed | `20261022-104336-859-BuildEditor` |
| Rules including inventory identity/privacy wire kind | 18 passed, zero warnings | `20261022-100743-754-Automation/Report/index.json` |
| Independent Editor inventory clients | 59 / 25 / 25 passed; actual echo 168.25 / 165.71 ms; both 96-item views 4,248 bytes | `20261022-102328-521-Inventory-150-25` |
| Retained variable-frame combat | 48 passed | `20261022-100926-374-CombatCheck-VariableFrames` |
| Retained V04 region | 93 passed | `20261022-100944-372-Region-DevelopmentCheck` |
| Independent Editor HUD/input | 26 / 37 / 37 passed; actual echo 156.13 / 156.31 ms | `20261022-100959-826-HUD-150-25` |
| Final Win64 game target | Passed | `20261022-104447-112-BuildGame` |
| Authored assets | 28 validated without writes | `20261022-104549-310-Bootstrap-Validate` |
| Final Win64 cook/stage/archive | Passed | `20261022-104649-962-PackageWin64` |
| Final cooked navigation with isolated, unique targets | 34 / 20 / 20 passed; actual echo 132.86 / 132.59 ms | `20261022-104908-678-Packaged-Navigation-150-25` |
| Final cooked inventory, lower-delay lane, base 29 / jitter 25 ms | 59 / 25 / 25 passed; actual echo 115.62 / 99.30 ms; both 96-item views 4,248 bytes | `20261022-105905-508-Packaged-Inventory-100-25` |
| Final cooked inventory, near 150 ms, base 53 / jitter 25 ms | 59 / 25 / 25 passed; actual echo 148.99 / 149.10 ms; both 96-item views 4,248 bytes | `20261022-110010-683-Packaged-Inventory-150-25` |
| Final cooked HUD/input | 26 / 37 / 37 passed; actual echo 141.65 / 142.07 ms | `20261022-105304-476-Packaged-HUD-150-25` |
| Final cooked filtered-view privacy | 12 / 29 / 27 passed; serialized views 843 / 708 bytes | `20261022-105420-531-Packaged-ViewNetworkCheck` |
| Final cooked full bag, 1080p | Captured and visually inspected | `20261022-105438-608-Packaged-InputUI-1920x1080-Inventory/Inventory.png` |
| Final cooked full bag, 720p / 125% text | Captured and visually inspected | `20261022-105450-789-Packaged-InputUI-1280x720-Inventory-125pct/Inventory.png` |
| Final cooked named Seal drop modal, 720p / 125% text | Captured and visually inspected | `20261022-105503-138-Packaged-InputUI-1280x720-DropConfirm-125pct/DropConfirm.png` |
| Final cooked real active reading, 720p | Real timer, initiating Item2 label and active reference captured and visually inspected | `20261022-105514-213-Packaged-InputUI-1280x720-ItemReading/ItemReading.png` |
| Final cooked dead bag, 720p | Empty bag/references and disabled item use captured and visually inspected | `20261022-105523-810-Packaged-InputUI-1280x720-InventoryDead/InventoryDead.png` |

`RunInventoryCheck.ps1 -Region` uses separate authority and two independent clients,
native key events and explicit widget/authority calls. Setup positions/protection,
1000-gold balances, unknown/known/full-bag books/Seal, one source-creation failure,
reservation setup, a death-only extra charge and a terminal deadline are labelled
engineering fixtures. Reading and return timers, ownership mutations, source
creation and terminal receipts use actual rules. Purchase replay creates one book and debits
forty gold once from the explicit fixture balance. Full-bag setup is not earned
vendor progression, economy balance, physical-pointer usability or human evidence.
The outside-release check invokes the native release handler with a synthetic
pointer event; physical capture/focus review remains separately pending.

Timing calibration is recorded separately from the target labels. The earlier
base-30 cooked inventory run passed 59/25/25 at 120.18 / 122.83 ms
(`20261022-103221-503-Packaged-Inventory-100-25`), and base 22 passed at
82.74 / 82.70 ms (`20261022-104054-434-Packaged-Inventory-100-25`). On the final
archive, base 26 passed at 82.77 / 82.91 ms
(`20261022-105053-304-Packaged-Inventory-100-25`) and base 49 passed at
132.81 / 132.85 ms (`20261022-105158-828-Packaged-Inventory-150-25`). Base 33
passed at 123.79 / 123.59 ms (`20261022-105606-319-Packaged-Inventory-100-25`),
and base 58 passed at 170.74 / 170.85 ms
(`20261022-105710-988-Packaged-Inventory-150-25`). These
runs prove their named behavior checks at their measured delays, not actual
100/150-ms transport. Earlier base-49 evidence on the preceding package measured
154.39 / 154.45 ms (`20261022-103327-414-Packaged-Inventory-150-25`). No
Source/Config changes occurred during each package's timing runs. Final calibrated
lane measurements are recorded in the table above: the lower lane has one client
near 100 ms and the other above it, not two measured 100-ms medians. Inspect actual echoes when
reproducing on another machine. All configured delayed lanes use up to 25 ms
one-way jitter. None of these fixtures establishes full-game traffic budgets.

Reproduce the focused lane with `Scripts/BuildEditor.ps1 -MaxParallelActions 4`,
`Scripts/RunTests.ps1`, then `Scripts/RunInventoryCheck.ps1 -Region -RoundTripMs 150
-JitterMs 25 -OneWayBaseMs 49`. Use `Scripts/BuildGame.ps1 -MaxParallelActions 4`
and `Scripts/PackageWin64.ps1` for the native package. The final cooked inventory
lanes used `-OneWayBaseMs 29` for the 100-ms target and `53` for the 150-ms target,
with `-JitterMs 25`; record the actual echo. Pass its `Archive/Windows`
as `-PackageRoot` to the inventory, HUD, navigation, view and capture scripts.
Run them sequentially with Source/Config fixed. `CaptureInputUI.ps1` supports
Inventory, InventoryDead, QuickItems, ItemReading and DropConfirm plus retained
B53 scenes, explicit dimensions and `-LargeText`. Inspect the PNG and JSON sidecar.

The final warm archive is
`Artifacts/20261022-104649-962-PackageWin64/Archive/Windows`. Its manifest records
B53 `b0af13e` plus the dirty B54 changes committed with this report. It does not
claim clean B53 already contained B54. Source/Config remained fixed from this
cook through the cooked probes. The source/configuration set contains 131 files;
the payload set contains seven executable/container/registry files. The local
hash audit is `Artifacts/B54PackageProvenance.json`, with zero source or payload
mismatches after the cooked probes. The final audit also records the commit. No authored/frozen binary, input or map asset changed.
This is a local warm build/cook, with a headless listen authority and two separate
owning clients in cooked network lanes. Dedicated source-engine delivery is separate.

The corrected navigation run's actual echo was 132.86 / 132.59 ms, despite the
150-ms target label. Its initial acknowledgement p95 was 165.79 / 165.96 ms and
route acceptance p95 165.79 / 165.96 ms. Each owner performed 14 native queries,
with 1.00 / 1.11 ms total query time. Sampled corrections outside initial fixture
settling reached 115.53 / 147.20 cm in the retained authority-initiated Step and
obstruction scenarios. These are engineering fixtures, not correction-free
prediction, ordinary combat death, or a full UI/frame/traffic performance pass.

The engineering captures record a 0.666 effective UMG viewport scale at 720p,
1.0 at 1080p, and an explicit 1.25 text preference for the larger-text lane.
Those values are not Windows display-scale measurements. Full-bag layout uses
seeded books/Seal and 160 gold; the real active reading capture additionally checks
its actual channel and initiating key. It does not establish earned progression.

## Failures found and corrected

UHT caught comma-separated reflected declarations; C++ caught inherited-member
shadowing and TObjectPtr deduction mistakes. The first network harness pressed
and released a key in one frame, so it never armed a ward and its following map
gesture legitimately moved. Events are now separated. A shutdown-order assertion
was moved before disconnect checking. Those failed runs remain in their original
artifact directories, including `20261022-094752-039-Inventory-150-25`.

The default bag and populated quick bar fit at 720p. The first 125% drop capture
exposed footer overlap (`20261022-095346-211-InputUI-1280x720-DropConfirm-125pct`);
confirmation now has an independent modal layer. A further full-bag capture with
a two-line Use label exposed twelve pixels of footer overlap; the final detail
viewport is 96 logical pixels and the whole action column can scroll. Final
book and Seal captures keep Drop and the footer separate. Armed reads take priority over
ordinary pickup, and pickup/drop recheck body sight as well as filtered observation.
The initial ItemReading image seeded the bag directly without delivering an acquisition
receipt, so its first receipt performed initial-life input cleanup and cancelled the
hold. The visual fixture now delivers its own snapshot before initiating use, as real
purchase/pickup already does, and asserts a real active channel before screenshot.
Independent clients separately check both the quick-key and bag/context reading labels.

The first cooked navigation regression, `20261022-103549-194-Packaged-Navigation-150-25`,
failed one of 34 server assertions (loss of observation), while both clients passed
17 checks. An unchanged rerun at `20261022-103853-129-Packaged-Navigation-150-25`
passed 34/17/17. Inspection found that the fixture chose a body only by proximity
within 20 cm: an ambient resident patrol from (-1100,-450), with a 250 cm radius,
can enter the scripted B target at (-1150,-500). That run did not log which body
was selected, so the exact cause is not established from the old log. The fixture
now holds ambient residents at their authored homes and asserts exactly one
authorized candidate in each of the three approach phases. The current navigation
runner requires 34 server and 20 checks per client. This changes probe setup only,
with no production movement or observation change. Both targets and the final
package were rebuilt for that correction; its cooked navigation check passes
34/20/20, including all six client uniqueness assertions. The failed run also logged occasional
0.285–0.343-second authority sample lateness; it is not a performance acceptance pass.

## Review boundary

B55 quote/versioned vendor, B56 consensual atomic peer exchange, B57 complete
presentation/focus/OS-scale matrix and B58 cold checkout, ten restarts, normal
peaceful UI journey, performance and uncoached review remain. No human participants
or OS scaling settings were invented. Current builds/cooks use a warm workspace.
The user-deferred 90-minute stress run remains deferred. Full conquest and full
60–90 minute pacing remain outside this checkpoint.
