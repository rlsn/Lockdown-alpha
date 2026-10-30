# B92 — physical bank access

30 October 2026 · B92 remains **in progress**

Native source **37ad129866053f92ba59c272ae248b373eeed9aa**. [Frozen evidence](evidence/B92Bank/README.md)
extends the [physical commodity service increment](B92_SERVICES.md).

All five selected market counters now expose the contender's one persistent
bank account. LMB opens the existing private counter panel. Deposit and Withdraw
buttons confirm 2, 10 or 50 gold once; there is no additional world-interaction
key, physical hold or interest. The panel shows the protected bank balance and
the deposit-fee rule. These fixed amount buttons are an initial interface;
arbitrary amount entry and accessibility review remain product UI work.

Depositing A debits A from the carried wallet and credits A minus ceil(2A/100)
to the bank. That fee burns. A deposit that would credit zero rejects. Withdrawal
transfers existing bank gold into the carried wallet without a fee. The bank
account belongs to the persistent seat, so opening another city's counter or
changing controller cannot reset it or create a second balance.

Requests share the existing living-body, protection, quiet, service-status,
free-hand, range and sight checks. Match/life/session and exact wallet/bank/bag
revisions are rechecked before committing. A canonical transaction changes both
accounts and the burn total together, then projects the private owner state.
Exact duplicate requests reuse the original result/receipt; changing the amount,
direction or revision cannot reuse a successful confirmation. Invalid or stale
requests spend nothing. Existing commodity confirmations remain separate from
bank actions within the same counter session.

The extended connected probe deposits three earned gold at Capital, checks one
fee burn and two bank credit despite a duplicate request, refuses stale bank
revision and zero-credit deposits, then retains that bank through actual remote
disconnect/replacement and withdraws two at Highfort exactly once without a fee.
Initial earnings use original Ore from real storage; later goods and travel pads
retain the explicit fixture boundaries of the previous service scenario.

Run the affected checks sequentially:

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -Gather
./Scripts/RunB92Services.ps1 -Capture
./Scripts/RunTests.ps1
./Scripts/BuildGame.ps1 -FullUnity -Gather
python -B Scripts/ExportB92BankEvidence.py docs/v09/evidence/B92Bank --verify
```

The connected suite passes **45 checks** with **21 commodity transactions** plus
one deposit and one withdrawal. Independent replay closes **177 receipts, 149
accounts, 570 historical item identities and 3,287 gold**: exactly one deposit-fee
burn and zero mint. Source-backed harvest/consume, stale and forged commodity
terms, quote races, capacity refusal and remote takeover retain their prior
coverage. The **59 rule tests** and both full-unity targets pass. All **207
protected inputs** remain unchanged. The previous frozen roster/economy/source
bundles remain independently checkable; they were not rerun in this narrower
service-only increment.

Shared official rare stock and paid book/item supply are the next service layer.
Finite deposits/work/crafting, earned full-map Witness/Charter, routines, security,
hostiles and legal contender AI remain open. This increment does not establish
an earned Charter journey, authenticated account reclaim, full active-match
recovery, a completed B92 stage, or B94 session/human/performance acceptance.
