# B66 — native economy services and feedback

23 October 2026. Implements B66 on B65 `e719758`. The user authorized continuing
and commits. Source/Config remain frozen during every Unreal operation, with
UE5.8.4 CL58902052 and compile worker cap4. No package or long/stress run is added.

The carried bag now shares one shell with seven focused tabs: Items, Books,
Markets, Craft, Orders, Treasury and Trade. Only the selected service is shown;
owned items, slots/weight, wallet and shortcuts stay alongside it. A responsive
service column and separate scroll areas preserve the bag at narrow widths.
Book, market, recipe, order, bank and trade controls use consistent rows/columns.
The inherited explicit drop and final peer-review modals retain their separate
focus scope. Pending transactions disable incompatible service changes.

Markets expose stock/cap, ask/bid, reserve, quote time and concrete capacity,
wallet, empty/full-stock, reserve and selected-input restrictions. Each Buy/Sell
is still one acknowledged unit. A new quote requires a separate review click;
review alone never purchases. Official materials show unlimited paid creation
and omit Sell, rather than presenting a fictitious finite reserve.

The owning controller retains only visited quote snapshots for its four services.
Closing or leaving a service displays a timestamped READ ONLY snapshot with no
exchange controls. Later rival stock changes do not update it at a distance.
Unvisited services display no invented prices. Valid match/life stamps are
required before a callback can replace the current quote or populate history;
old match entries are cleared before presentation. No cache field is replicated.

Craft previews name the exact selected bag slots, inputs, fee, output, net slots/
weight, coupon/site/expiry, duration and eligibility reason. Equipped Survey and
Analyze retain score/cooldown requirements. Orders show the actual reward, global
quota, own completion limit and distinct endorsement. Surplus remains owned.
Activation previews show registration/spent/recovery, overlapping-window and
full-deadline restrictions. Character-panel activation now opens this review.
Closing and arming spends nothing; a fresh mapped context gesture starts the
channel. The armed preview is consumed by that first request, so retries require
a current explicit preview. Releasing, movement, damage or reopening a major
panel cancels through the retained shared reservation authority.

Treasury uses an explicit amount field and separate review/confirm controls.
The Charter review names wallet share plus protected bank remainder equaling100,
with retained prerequisites and the risk of losing the remaining activation
opportunity. Editing the amount or changing session/account revisions invalidates
review. A deadline rejection explains the required94 seconds. Deposit/withdrawal
and Charter checkout still use the same authoritative service/session boundary;
no pricing, stock, payment or procurement authority moved into the UI.

Items continue to label cargo as commercial input, with quick Use/Assign disabled.
Normal profiles omit the obsolete physical-objective inventory filter. Peer
exchange uses the same own-bag shell, retains voluntary offered-item visibility
and both players' existing review/confirm flow, and releases reservations on
cancellation. Ordinary bodies and their private wallets/builds remain masked.

The HUD and context use shared owner/public presentation helpers: private next
step, endorsements/Witness, registration recovery, public warning/opening, and
whether a new ten-second claim still fits. Courier and nearby Heart context
remain actionable. The public opening copy states that no Charter is required.
Public text accepts only GameState facts; own progression accepts only the owning
state. The eight-field observed tuple and34-field public allowlist are unchanged.

The native checks exposed a delayed shortcut leak from focused editable text.
The bag's preview input now consumes mapped inventory/context key-down while
editing, while Slate character events still type those letters normally. Tab,
Shift-Tab, Enter and Escape retain the shared panel policy. Checks wait for actual
focus and later input dispatch, rather than trusting a same-tick state assertion.

Verification on the final source:

- Editor build: `Artifacts/20261023-233047-421-BuildEditor`; four actions,
  16.91 seconds, UE5.8.4 CL58902052, worker cap4.
- `Scripts/RunEconomyUICheck.ps1`:
  `Artifacts/20261023-233109-122-B66-EconomyUI`; **84 native checks in each of
  three layouts, zero failures**. Small:1280×720; Large:1280×720 with125% text;
  Wide:1920×1080. All39 native PNGs were written;13 representative originals
  are retained, with hashes/provenance for all39.
- Native Slate Tab/Shift-Tab visits and wraps only visible enabled controls;
  logical targets are at least40×40. Search actually receives focus and types
  the inventory/context letters 'ik'; later dispatch leaves the panel open and
  starts no world action. Backspace key-down plus Slate character8 clears it
  and restores bag controls. Enter closes/arms without spending; actual remapped
  K through Enhanced Input requires release/fresh gesture and starts the shared
  production hold. Release and major-panel reopening release inputs without fee.
- Real paid purchases exercise acknowledgment/pending double-click rejection,
  changed quote review and frozen distant history after rival stock mutation.
  Wrong match/life callbacks cannot replace the quote. Actual observed-peer
  invitation/accept/offer/cancel exposes only offered Fiber/Ward and releases
  reservations. This new trade fixture covers editing/cancellation; it does not
  claim a new Ready/Confirm transfer or remote peer-modal acceptance.
- Each UI ledger independently replays14 receipts,7 accounts and93 unique item
  identities:450 start +957 labelled diagnostic gold -0 burns =1407 gold, with
  zero authority audit failures. Seven labelled actual cargo grants and temporary
  own endorsement/Witness/public-window setup make this an engineering UI fixture.
  Its purchases are real transfers, but this is not the earned progression lane.
- `Scripts/RunCharterCheck.ps1` regression:
  `Artifacts/20261023-233344-289-B65-Charter`; **23 rules,56 authority checks and
  39 earned-route checks, zero failures**. Both final ledgers pass independent
  replay. Authority:38 receipts,97 identities,450+1000 diagnostic+580 rewards
  -450 burns=1580. Earned journey:28 receipts,103 identities,450+205 rewards
  -178 burns=477 aggregate; own wallet15/XP300, rival without Charter wins.
  The earned lane disables diagnostic items and uses actual Witness/couriers/
  purchases/crafts/orders/checkout with controlled clock/placement/visit samples.

Portable reports and ledgers are in [evidence/B66](evidence/B66/Summary.json).
[SourceManifest](evidence/B66/SourceManifest.json) hashes189 final physical source,
config, runtime catalog and verification files. [Capture provenance](evidence/B66/Captures.json)
records all39 PNG hashes/dimensions and13 unmodified retained copies.
[Report provenance](evidence/B66/ReportProvenance.json) binds normalized portable
reports/ledgers to their raw artifact paths/hashes. The successful build log is
retained. Source/Config stayed frozen during every Unreal operation; no runtime
change followed the final successful build/checks.

Representative captures inspected on the final run:

| Capture | Evidence |
| --- | --- |
| [Small market](evidence/B66/Captures/Small-Market.png) | Finite stock/cash, ask/bid, own bag and one-unit controls |
| [Small saved quote](evidence/B66/Captures/Small-QuoteHistory.png) | Timestamped READ ONLY snapshot; exchange disabled |
| [Small workshop](evidence/B66/Captures/Small-Workshop.png) | Actual bag restored after native search clear; exact inputs/fee/final capacity and fresh-hold arm |
| [Large official supply](evidence/B66/Captures/Large-OfficialSupply.png) |125% text, paid unlimited materials, no fake Sell |
| [Large Treasury](evidence/B66/Captures/Large-Treasury.png) | Explicit100+0 split, deadline risk, separate review/confirm |
| [Wide trade](evidence/B66/Captures/Wide-Trade.png) | Own bag and only voluntary offered items |

Service and bag panes scroll independently; captures intentionally show different
scroll positions, while native keyboard traversal reaches their offscreen controls.
An early same-frame input assertion missed a delayed shortcut leak; the final
checks wait for later dispatch. Early screenshot timing also captured a panel
after it had closed. The fixture now freezes each capture before its next action
and verifies Workshop visibility at screenshot request time. A later fixture-only
Backspace key-down omitted Slate's character event; final checks include it.
Only the final complete successful run above is the acceptance evidence.

Full remote clients, packet privacy/latency, current cook/package, normal timed
sessions, restarts/performance and human/physical-input/OS-scale acceptance remain
B67. Offscreen rendering used the primary monitor's read-only100% scale query;
it does not certify Windows scaling. These checks use native widgets/input with
labelled engineering setup, not human playtests. No long/stress run or push was
requested or performed.
