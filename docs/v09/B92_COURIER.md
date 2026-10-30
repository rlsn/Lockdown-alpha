# B92 — physical courier lifecycle

30 October 2026 · B92 remains **in progress**

Native source **d0b9163a7d7a956d374bc727beea6fa30f23b406**. [Frozen evidence](evidence/B92Courier/README.md)
extends [physical work](B92_WORK.md).

## Contract and control

LMB opens any of twelve authored courier boards. The Courier panel shows one
eligible destination, strategic distance, remaining issuance/payment budgets and
the 25-gold/45-XP reward. Confirm acceptance once to receive a real sealed parcel.
At its assigned board, confirm delivery once and channel for three seconds.
Release continues; S or lost eligibility stops without spending the parcel or
payment quota. The active destination remains in the owner's private tracker
after leaving the counter. No new interaction key or free source ability exists.

Offers use shortest-path distance over the public strategic graph. Edge weights
are the published rounded horizontal polyline lengths, not straight-line distance
or the length of a fitted navigation mesh. Endpoints must be distinct and the
route must be 25,000–80,000 cm. Offer rotation depends on that contender's bounded
issuance count, without consulting hidden rival facts. No extra town market is
created.

The selected graph gives Capital Guild, Scholars Market and Bellwick no in-band
peer board. They expose no issuance offer. The implementation keeps the selected
distance rule; endpoint placement/route coverage is a visible design question.

## Physical proof and bounded rewards

The server issues one sealed contract parcel (appended item wire value98, weight1)
and links its original identity to the contract, owner, endpoints and issuance
receipt. It is a contract token outside the equipment catalog: it cannot be sold
as a commodity, read as a book or prepared as an item. Ordinary pickup, storage
and dropping use the existing physical inventory/world paths.

Only the issuing persistent contender can deliver the original parcel. A rival
may take, transport and drop it but cannot redeem it. Death drops it without
ending the job, resetting either budget or creating a replacement. The original
owner can return, recover it and finish. Available-seat controller replacement
preserves that same contract; authenticated reconnect remains separate work.

Acceptance atomically creates the physical token and spends one of40 issuance
units. One active job is allowed. Successful delivery atomically consumes the
token, spends one of20 payment units, and mints25 gold;45 XP is awarded only
after success. No legacy commission credit is granted. Exact retries return the
same result/receipt. A changed life, bag, wallet, contract revision, token,
destination, input or eligibility refuses without partial payout.

Abandonment at a board records a terminal receipt without payout or refund. Its
parcel becomes void cargo and remains a real item until otherwise handled; it
cannot complete a later job. Abandonment does not remotely erase another
carrier's bag. All contracts, including abandoned ones, retain causal history
in authority evidence. Full recovery/cleanup policy remains later integration.

## Validation and limits

The courier suite passes **23 connected checks**. Independent replay closes
**307 receipts,149 accounts,629 historical identities (607 live) and3,834 gold**:
3,288 start +550 courier mint -4 death tax. Sixty canonical parcel creations
lead to22 paid consumes and38 void ground parcels. Exactly two deliveries use
normal remote channels;20 other payouts and38 abandonments are labelled
quota-bound authority fixtures. The 720p capture verifies the private destination,
remaining budgets, reserved bag parcel and delivery progress bar.

Rules59, connected services/work83, economy125, four-controller roster37,
B91 source/control53 and combat50 pass. Both full-unity targets pass. All207
protected assets and supplied inputs remain unchanged.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Courier.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunB92Economy.ps1
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/RunB91Combat.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather -MaxParallelActions 4
python -B Scripts/ExportB92CourierEvidence.py docs/v09/evidence/B92Courier --verify
```

The connected scenario stages travel positions to isolate endpoint access and
lifecycle. It does not establish a naturally walked courier route or normal
earnings/pacing. Two deliveries use normal remote owned requests and real
three-second channels. Death, rival pickup/drop, owner recovery and actual
disconnect/replacement are exercised around the same second token. Separate
labelled authority fixtures perform20 physical-token deliveries and38 abandonments
to close the payment/issuance bounds without claiming20 ordinary trips/channels.
No reserve transfer, diagnostic gold grant or extra token allowance is used.

Retained diagnostics distinguish a probe-only private-helper compile failure,
the invalid assumption of an in-band Capital offer, and a probe that armed a
drop without confirming its ground point. The final run uses the normal
confirmed drop path. Edge weights were checked against the published horizontal
strategic lengths before accepted native execution.

This is transaction/lifecycle acceptance. Complete earned Witness/Charter play,
useful book acquisition/learning/preparation, courier route readability, natural
travel/competition, concurrent last-quota races, broader interruption/recovery,
resident/security/hostile behavior and legal contender AI remain open.
`v09_full` stays closed; B93/B94 acceptance is separate.
