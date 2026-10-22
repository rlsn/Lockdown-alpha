# B56 consensual peer exchange

22 October 2026. Predecessor: `ccddfae` (B55). Native Unreal 5.8.4 CL 58902052,
CharacterMovement, Enhanced Input, UMG/Slate and filtered authority remain.
This checkpoint implements peer exchange; B57–B58 still complete v0.5.

## Authority and restricted views

T or the inspector's neutral Offer trade action sends a gesture for an authorized
observed humanoid. The authority resolves that observation and checks both bodies,
120 cm range, body sight and mutual observation, life, protection, occupied actions,
six seconds without positive damage dealt/taken, and invitation/session exclusivity.
All observed humanoids expose the same affordance. Residents can decline through
the same Unavailable result. Initial availability responses and valid invitations
use the next 250 ms server bucket; no hidden human/NPC flag or diagnostic reason
is serialized. Stable server wallet identity limits attempts per target to ten
seconds, including unavailable attempts and returned bodies.

Invitations expire after 30 seconds. Accept starts a fresh session with 60-second
inactivity and 180-second hard bounds. Only accepted edits or first valid consent
changes extend inactivity; duplicate requests, polling, rejected changes and wallet
refresh do not. Match/life, request floor, bounded queue/rate and receipt caches
protect the command path. Cancellation takes precedence over queued consent and
blocks earlier in-flight invitations. A final committed receipt wins over a late
cancel; it is never reported as rolled back.

TradeView contains the owner's voluntarily offered terms, partner-offered terms,
consent, version/digest, validity and lifecycle. Own tray references are already
owned item IDs; partner tray handles are random per session. It contains no
partner wallet balance, remaining bag, build, role, canonical actor or contender
identity. Each receipt carries only its recipient's existing own-state snapshot.
The observed eight-field mask and public 22-field allowlist are unchanged.

## Reservations and two-stage consent

Each side offers at most six nonstacking books, finite charges or a Seal and an
integer gold amount from zero through one billion. Gold remains in the actual
stealable/death-taxed wallet. Any wallet or inventory revision change revises the
offer, clears both Ready/Confirm flags and preserves the submitted gold amount,
even when now unaffordable. Only the owner sees its exact balance and detailed
validity; the partner sees Offer needs review. No escrow or trade invulnerability
is introduced.

Adding/removing an item reserves/releases its real physical record and increments
item/container versions. Quick references remain present and locked while reserved;
cancellation unlocks them, while transfer clears the exact outgoing references.
B54's bag-validity invariant was extended to permit these locked references;
assignment and use continue rejecting reserved items. Existing projections remain
synchronized through InventoryDirector. Reserved use/drop cannot release and consume
an offer. Other accepted outgoing gameplay actions cancel the session. Accepting
exchange invalidates the authoritative vendor session; opening the vendor cancels
trade. This uses server session state, never the controller's client-only quote.

Every accepted edit revises a server-computed digest and clears all consent.
The digest includes only the session/revision and offered gold, temporary handles
and definitions. Private life counters and physical availability versions are
checked separately; they are not exposed through a guessable digest input. Ready
validates current terms and projected final capacity. Both Ready enter Review;
each participant then presses a separate Confirm for the displayed session,
revision and digest. Widgets cache the terms actually rendered, so an old click
cannot silently approve an arriving revision. Pending edits/consents disable repeats.
A completely empty exchange rejects; a consensual gold-only gift is valid.

At the second confirmation all eligibility, item ownership/reservations, wallet
coverage, signed arithmetic, final capacity and one-Seal limits are rechecked.
Outgoing items free their slots before incoming assignment in one synchronous
transaction; neither side is published midway. Physical identity and Seal carrier
projections move together, both bag versions update, then owner receipts publish.
There is no fee, XP, credits, Witness or renewed fabrication allowance.

Due combat/death precedes noncombat work. Death/disconnect cancels reservations
before mandatory loss and drops. Inventory, vendor, development, regional and
objective work retain their authority order; trade samples afterward, observes
incoming wallet changes and rejects terminal Heart/deadline commits. No user-facing
safe mode or special theft rule is added.

## Shared native panel

Invitations, Editing, Waiting, Review, Confirmed/waiting, Completed, Cancelled and
Expired use the existing major bag shell and feedback. Both offer directions and
gold totals, own bag/capacity, reserved labels, gold entry, item add/remove, Ready,
separate Confirm and Cancel are visible. Offer trays and bag/details scroll; the
match clock remains live. Close/Escape and focus cancellation cannot reopen from
a late reply. A dismissed completed receipt is not reopened by replay.

## Verification

Evidence is under repository-root Artifacts. Native and final-package evidence (historical checkpoints keep their original source provenance):

| Check | Actual result | Evidence |
| --- | --- | --- |
| Native exchange / lethal ending / 720p 125% text | Server 38 / A 12 / B 12; actual echo 149.03 / 148.96 ms | `20261022-161915-365-Trade-150-25-Death-UI1280x720-125pct` |
| Rules before final privacy assertion | 18 passed, zero test warnings | `20261022-162048-351-Automation` |
| Retained native inventory | Server 59 / A 25 / B 25 | `20261022-162129-194-Inventory-150-25` |
| Final native exchange / terminal ending / 1080p | Server 42 / A 12 / B 12; actual echo 99.06 / 98.89 ms | `20261022-162710-801-Trade-100-25-Terminal-UI1920x1080` |
| Variable-frame combat | 48 passed | `20261022-162249-958-CombatCheck-VariableFrames` |
| Retained explicit V04 region | 93 passed | `20261022-162312-894-Region-DevelopmentCheck` |
| Final Editor build | Passed | `20261022-164528-234-BuildEditor` |
| Final privacy/rules suite | 19 passed, zero test warnings | `20261022-164148-067-Automation` |
| Final Game build | Passed, MaxParallelActions 4 | `20261022-164557-215-BuildGame` |
| Asset validation | Existing 28 packages validated without regeneration | `20261022-164649-952-Bootstrap-Validate` |
| Win64 Development cook/stage/archive | Passed, cook zero errors/warnings | `20261022-164716-551-PackageWin64` |
| Cooked exchange / lethal ending / 720p 125% text | Server 43 / A 12 / B 12; actual echo 132.87 / 149.15 ms; target 150/base 53/jitter 25 | `20261022-164842-692-Packaged-Trade-150-25-Death-UI1280x720-125pct` |
| Cooked exchange / terminal ending / 1080p | Server 42 / A 12 / B 12; actual echo 98.98 / 99.26 ms; target 100/base 29/jitter 25 | `20261022-164950-170-Packaged-Trade-100-25-Terminal-UI1920x1080` |
| Cooked exchange / actual disconnect / 720p | Server 41 / A 12 / B 11; actual echo 149.26 / 149.21 ms; target 150/base 53/jitter 25 | `20261022-165055-432-Packaged-Trade-150-25-Disconnect-UI1280x720` |
| Cooked retained earned vendor | Server 23 / A 19 / B 14; actual echo 149.20 / 149.13 ms | `20261022-165200-644-Packaged-Vendor-150-25` |
| Cooked retained inventory | Server 59 / A 25 / B 25; actual echo 149.36 / 149.10 ms; full-drop view 4248 bytes | `20261022-165451-236-Packaged-Inventory-150-25` |
| Cooked HUD/input | Server 26 / A 37 / B 37; actual echo 132.81 / 132.65 ms; target 150/base 49/jitter 25 | `20261022-165556-478-Packaged-HUD-150-25` |
| Cooked restricted-view privacy | Server 12 / A 29 / B 27; maximum serialized client views 775 / 742 bytes | `20261022-165712-751-Packaged-ViewNetworkCheck` |
| Cooked navigation regression | Server 34 / A 20 / B 20; actual echo 148.89 / 132.75 ms; target 150/base 49/jitter 25 | `20261022-165730-536-Packaged-Navigation-150-25` |

Final package Review, Completed and TradeSixItems images were visually inspected
in each of the three cooked trade directories above (nine images total), at 720p
normal text, 720p 125% text and 1080p normal text. The 1080p native terminal images
and earlier 720p native engineering captures were also inspected. Both directions,
gold summary, Confirm/Cancel and receipt Close remain visible; the six-item trays
and bag/details scroll. This verifies these native layouts, not full OS scaling,
physical-pointer/keyboard traversal or the B57 confirmation sublayer. The initial
aborted Editor capture displayed a video-memory warning; the final cooked captures
above did not. No performance acceptance is inferred from a readable screenshot.

The final warm archive is
`Artifacts/20261022-164716-551-PackageWin64/Archive/Windows`. Its manifest records
the B55 parent with sourceDirty=true because B56 was validated before commit.
All 145 Source/Config hashes and seven executable/container/registry hashes match
that archive and final source. `Artifacts/B56PackageProvenance.json` binds the
post-commit audit. No Source/Config edits follow the final cook. The existing
CrowdFollowing/Recast warnings remain in asset validation; its bootstrap still
succeeds, and the subsequent cook has zero warnings. Earlier trial archives and
failed probe evidence retain their original provenance.

Run `RunTradeCheck.ps1 -PackageRoot <archive> -RoundTripMs 150 -JitterMs 25
-OneWayBaseMs 53 -CaptureUI -LargeText` for the death ending; use target 100/base
29 with `-Terminal -CaptureUI -Width 1920 -Height 1080`, or target 150/base 53
with `-Disconnect -CaptureUI`. Server and two remote clients run independently;
other Unreal lanes run sequentially. Packaged authority is the labelled headless
listen topology with a nonparticipating local connection, not dedicated-source
packaging. Environment files record injection settings, requested viewport/text
scale and ending. Client JSON records actual median transport echo; a target name
is not an RTT measurement. Each rendered run captures Review, Completed and both
six-item trays from actual authority/own state.

The independent-client probe explicitly seeds 100/20 wallets, 24/23 bags, books,
charges, a Seal and retained craft receipts, body locations and protection. Actual
observed invitation, edits, Ready and two confirmations use native clients/widgets.
A stationary NPC fixture performs the real one-second Pickpocket channel with a forced
successful roll; ordinary retry, bounded twelve-gold transfer and wallet semantics
remain. Later direct authority sessions isolate numeric, reservation, capacity,
stale-consent, timeout-age and service-switch cases. Final lanes separately exercise
actual due lethal melee/death, actual No acquisition deadline or remote disconnect.
These are engineering fixtures, not earned trade progression, human acceptance,
normal lifetime duration, physical WAN conditions or OS display-scale evidence.
Timeout ages and overflow balances are injected only in isolated cases; they do not
claim that a normal three-minute exchange or real excessive wallet was observed.

The first cooked lane, `20261022-163636-584-Packaged-Trade-150-25-Death-UI1280x720-125pct`,
found a further fixture issue: the spawned resident could wander on cooked navigation,
so the ordinary stationary-channel rule cancelled its Pickpocket before any roll or
wallet transfer. The fixture now disables that NPC's autonomous actor tick; real
hold refresh, timing, geometry, retry and transfer rules stay active. The lethal
impact fixture is also held stationary. No ordinary resident behavior changed.
The disconnect probe gives the survivor an extra second to inspect closure before
its completion phase, avoiding a test-stage race under packet delay.

## Corrections and limits

Failed compilation evidence remains at `20261022-155508-127-BuildEditor` and
`20261022-160804-540-BuildEditor`. The aborted theft-fixture run remains at
`20261022-161102-480-Trade-150-25-Death-UI1280x720-125pct`. The next native
run (`20261022-161626-193-Trade-150-25-Death-UI1280x720-125pct`) passed both
clients but found two server-fixture assertions: binding a quick slot changed the
inventory/trade revision, so its following direct fixture edit was correctly
rejected as stale. The helper now refreshes the authoritative revision before
submitting its edit and checks that the reservation exists before attempting use.
The subsequent 38/12/12 run passes those actual reserved-use/cancellation checks.
The final expanded native terminal lane passes 42/12/12. A warm UBT makefile
initially omitted the newly added privacy rule source; module discovery was
regenerated and the rule runner now requires that named test as well as the
expected total, rather than accepting the old count.

Initial builds found integration include-order and TObjectPtr deduction errors,
then a missing book declaration in the probe. They were repaired before native
validation. The first client run used roll 1 while expecting successful theft;
the actual failed theft correctly transferred nothing. That failed fixture run
was stopped and retained, and its roll corrected to 20. Inspection also replaced
arbitrary map/set eviction with bounded FIFO receipt/completion histories so a
newly inserted receipt cannot evict itself.

B57 shared presentation, focus/keyboard and full resolution/OS scaling, and B58
cold checkout, restart, normal 25-minute UI journey, performance and uncoached
human review remain. The local build/cook is warm. The user-deferred 90-minute
stress run remains deferred; remote distribution and dedicated source-engine
packaging remain conditional/unverified.
