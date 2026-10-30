# B92 — prepared Survey at finite Crystal deposits

30 October 2026 · B92 remains **in progress**

Native source **dd3545f31344099087a6ac2d26c4ebbce4b8c2cf**. [Frozen evidence](evidence/B92Survey/README.md)
extends [automatic deposits](B92_DEPOSITS.md).

## Delivered ability path

Prepare a learned Survey in a shared action slot, activate the slot, then confirm
the Crystal deposit on the ground once. The character channels for one second;
input release does not cancel it. S stops the channel. No generic interaction
key, free mining ability or new item is added.

Survey uses the inherited skill task authority: learned rank, matching equipped
slot/build, score12 and a six-second cooldown. Score is effective WIS plus rank
and applicable subclass contributions. The inherited Survey path charges no MP
or gold. Cooldown starts on acceptance and remains if an accepted channel is
stopped. Rejected score/capacity/cooldown requests do not start another cooldown.

Five server-only Crystal task targets are anchored to the selected deposit POIs.
The full-map action uses the existing full-coordinate wire format and public
ground selection, rather than exposing authority actor IDs or using the small
legacy observation grid. Actual120 cm reach, floor continuity, body sight,
stationary/quiet eligibility, life/protection, free hands, stock and capacity
are validated. Current preparation and score are rechecked before completion.
Silence blocks this learned action; Mute alone does not. Ore and Fiber retain
their six-second automatic operation and are not Survey targets.

Accepting Survey suppresses automatic gathering at that deposit. Completion
spends one of the **same six match-wide charges** and creates one canonical
Crystal with a named Survey receipt. It creates no alternate allowance, XP,
gold, respawn refill or replacement source. All-nine depletion still produces
exactly54 total goods across both extraction paths.

## Evidence and limits

The connected suite passes **67 checks**. Independent replay closes **285
receipts,149 accounts,627 historical item identities and3,103 gold**, with zero
mint and the previous185 bank/official burn. Exactly two named Survey extraction
receipts contribute to the same54 total deposit outputs; no extra quota appears.
The 720p capture records the prepared Survey progress display. Rules59, economy125,
four-controller roster37, B91 source/control53, full-map combat50 and both
full-unity targets pass. All207 protected inputs remain unchanged.

The connected scenario stages a valid single-slot learned Survey build, checks
score10 refusal, raises the fixture score to12, and confirms through the normal
slot/ground path. It checks successful extraction/cooldown, immediate retry,
Mute permission, Silence refusal, S interruption with retained cooldown, and
full-bag refusal. Two successful Survey outputs share the existing Crystal
budget with the ordinary automatic extraction. The remaining source drain
retains its explicit authority-fixture boundary.

Preparation/attributes are **diagnostic**, not an earned build. This does not
prove natural book purchase/learning/preparation, affordability, all-five-target
travel, concurrent Survey races, full interruption ordering or human usability.
The initial failed case is retained separately; the fixture now clears duplicate
Survey slots independently of which persistent seat was admitted.

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/RunB92Economy.ps1
./Scripts/RunB92Roster.ps1 -Humans 4
./Scripts/RunB91Network.ps1 -Saplings
./Scripts/RunB91Combat.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather
python -B Scripts/ExportB92SurveyEvidence.py docs/v09/evidence/B92Survey --verify
```

Next: physical contracts/crafting, full-map prepared Witness and earned zero-kill
Charter progression, including actual acquisition and preparation. Routines,
security/hostiles, legal contender AI/tier transitions, authenticated reconnect
and full recovery remain open. `v09_full` remains closed; B93/B94 acceptance is
separate.
