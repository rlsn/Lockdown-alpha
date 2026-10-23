# B61 — goods, accounting and physical banking complete

23 October 2026. **B61 engineering gate is complete and verified.** This closes
the partial cargo/accounting/banking slices; B62–B67 are still pending. It does
not claim a playable complete v0.6 economy or all E01–E48 acceptance.

## Delivered

- Five goods, 24-slot/net-weight acquisition and preparation checks, drop/pickup,
  commodity quick-use exclusion and the native bag's weight display.
- Versioned staged runtime catalog. It validates physical weights against the
  common inventory definitions and initial three-market stock/cash/price tables.
  Recipe/schedule/service data remains available for later milestones; those
  systems are not enabled by parsing their companion JSON.
- Once-only match-owned institution state: 450 market gold and 84 actual uniquely
  identified physical stock instances, with independent institution addresses.
  The two outcrop budgets hold three unrealized charges each. Global order
  limits use locked N; persistent per-contender courier/sentinel/Core/Supply
  budget identities are initialized. B62–B65 will bind their new transactions.
- Private physical Treasury banking at the existing Guild site. Native amount,
  Review and separate Confirm use authoritative sessions, exact revisions,
  range/body sight, alive/unprotected/quiet checks and queued commits after
  damage/death. Deposit fees burn; withdrawals transfer. Death/theft see only
  carried gold; bank and retired balances remain in aggregate totals.
- Causal gold/item journal: exact deltas, expected revisions, finite budget
  debit, create/transfer/consume operations, immutable origin/parent lineage,
  replay-safe committed receipt identities and independent full-history replay.
  Every mutation is validated before the ledger commit. Existing native action
  authorities retain their own validation/serialization; their audit hooks run
  after mutation and report unexplained changes rather than repairing them.
- Carried endowment, paid book/legacy Seal creation, book reading, ward loss,
  pickup/drop, peer transfer and death paths now populate the same journal.
  Paid item creation shares the payment receipt. A ward charge transfers into
  its timed source under the same identity and is consumed on expiry/destruction
  or owner disconnect, without counting the carried charge twice.
- Physical-world reconciliation checks all live items against the journal and
  owner bag records, including duplicates, missing identities, holders, slots,
  revisions, reservation flags, types and causal metadata. Institution addresses
  come from physical records, not inferred from ledger locations. Gold audits
  include player wallets/banks and actual realized NPC purses. Lazy purse
  endowment is counted once; body rebinding does not endow again.
- Named Endowment/MatchClose/Reset exports persist accounts, endowments, retired
  balances, item history/lineage, budgets, catalog identity and exact decimal
  int64 deltas. Match UUIDs prevent evidence being overwritten by later matches.

## Verification

Pinned UE5.8.4 CL58902052 / MSVC14.44.35229 / SDK10.0.26100.0. Compiler cap four;
Source/Config was frozen during each Unreal build/test operation.

- Editor build passed, final test corrections: five actions / 21.59s,
  `Artifacts/20261023-140853-466-BuildEditor/BuildEditor.log`.
  Earlier full integration build: 40 actions / 164.75s at
  `Artifacts/20261023-140152-360-BuildEditor`.
- `Scripts/RunEconomyCheck.ps1 -TimeoutSeconds 120` **passed**: all **22 native
  rules** (including CargoCapacity, EconomyAccounting and EconomyCatalog) and
  **30 standalone authority checks**, zero failures. Evidence:
  `Artifacts/20261023-140934-748-B61-Economy`.
- The authority fixture exercises real physical bank open/queued duplicate and
  committed replay/stale revisions/range loss; paid book accounting and reading;
  detection of duplicate identities/altered provenance; actual ward placement
  and expiry; cargo death/drop/return/recovery; bank survival; actual lazy NPC
  purse creation/body rebinding; retired balances and world/journal audit.
- The fixture explicitly mints 200 diagnostic gold and creates one diagnostic
  Ore. It uses a death/return time jump. These are labelled fixtures, not normal
  earning, economic progression, independent clients or human input evidence.
- Independent Python replay of actual exported Endowment, B61Verified and Reset
  JSON **passed**. Opening record: 450 gold / 85 items. Final: **486 endowed +
  200 diagnostic mint - 56 burned = 630 gold**, six accounts, 87 unique item
  identities, 13 receipts. The 36 additional endowment is an actual realized
  resident purse, not invented recurring income. Final live items are 84 market
  goods plus recovered Ore; the book and ward are explicit consumed records.
  The Reset export includes retired wallet/bank balances. Zero audit failures.
- Specification model **passed**: 229 quote states and budget/bank/cargo/timing
  arithmetic. `git diff --check` passed. Runtime/spec JSON bytes match.

The first executable check found a test expectation using floor at the int64
fee boundary and a ward fixture at 100 cm against the retained 96 cm limit.
Both test inputs were corrected; the bank rate limiter was replenished in the
fixture so its queued range-loss case actually reaches commit validation.
Failed evidence remains at `Artifacts/20261023-140641-488-B61-Economy`.
Previous 60-second startup timeouts remain historical; current native passes
supersede their execution-pending status without changing those original logs.

Portable results are retained in [evidence/B61](evidence/B61/Rules.json). Recheck
exported receipts with `python Scripts/CheckEconomyEvidence.py <export.json>`.
The runner disables editor tooling plugins for bounded verification, as existing
native probe runners do; no engine/project-plugin migration was made.

## Boundaries and next work

Native screen layout/input, OS scaling, real remote clients, complete normal
sessions, fresh cook/package and human review remain B66/B67 acceptance. No such
result is inferred from the authority fixture. Legacy engineering probes that
directly alter wallets/spawn loose fixtures need explicitly journaled diagnostic
setup when extended; unexplained grants are intentionally audit errors.

B62 is next: stable paid Fiber/Ore supply and actual finite regional Buy/Sell,
with quote revision/race/cash/capacity handling. Then production/skills (B63),
scheduled institutions (B64), distinct endorsements/public Charter (B65), full
native economy presentation (B66) and release verification (B67). The ordinary
frontend retains v0.5 progression until B65 replaces its credits/physical Seal
route; B61 does not silently advertise the final public contest as delivered.
