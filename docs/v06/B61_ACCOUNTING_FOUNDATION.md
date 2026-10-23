# B61 second slice — institutional state and accounting

Historical checkpoint. [B61 completion and current verification](B61_IMPLEMENTATION.md) supersedes the pending status below.

23 October 2026. B61 remains **partial**. This slice adds the server accounting
domain, actual initial institutional stock, runtime data staging, and gold
journal integration. It does not complete a playable v0.6 release.

## Source review and implementation

The starting checkpoint is `a01c04f` plus the transcript attributes correction.
B60 and the first B61 report correctly describe the delivered source: cargo is
implemented, and the remaining economy was absent. Master revision 6 and the
complete v0.6 contract remain authoritative. The ordinary profile retains the
legacy v0.5 progression until B65 replaces it; no public Charter acceptance is
claimed.

`Content/Lockdown/Data/economy_rules.json` is the versioned runtime copy of the
specification companion. UBT stages it as UFS data for packaged reads. The
catalog validates schema, every physical weight, retained slots/duration,
institution identities, integral stock/base/cash, and finite node defaults.
Invalid reloads preserve the previous valid catalog. The content checksum is
reported at initialization. Recipe, service and scheduled demand sections are
still companion data; they are not yet bound to gameplay.

At an ordinary UI-profile match start, a server-only `LDEconomyDirector`
initializes exactly once after lobby validation. It allocates all 84 unique
market items before committing the initial stock receipt; any allocation failure
destroys the candidate items and leaves the lobby unstarted. Market cash totals
450. Physical stock lives with the institution, separately from ordinary NPC
purses and outside replicated observation. There are no market service screens
or buy/sell requests yet. Outcrop1/2 charge budgets are three each, separate from
live Crystal item counts. Core/Supply/shortage global limits use the locked
starting roster, not the later number of connected contenders.

`LDEconomyLedger` records exact account deltas and expected revisions, item
create/transfer/consume transitions, origin/parent IDs, mint/burn classification,
and finite budget debits in a single validated commit. Replaying the same full
transaction returns its existing result without changing state; changing its
terms under the same receipt ID fails. Failed validation changes no balance,
item, budget or committed receipt. Independent replay checks account totals,
revisions, retired balances, item locations/lineage and remaining budgets.
Physical output allocation is the caller's responsibility before commit. This
is server domain code, not a client endpoint or an alternative reservation lock.

Existing courier/sentinel rewards, official books, legacy fabrication fees,
death taxes, peer transfers and successful Pickpocket transfers now call a
causal gold audit after their existing authoritative mutation. That adapter
compares actual persistent contender and NPC wallets against the journal,
records per-account deltas, and reports unexplained changes as reconciliation
errors. It does not silently classify a diagnostic grant as earned money or
roll back existing gameplay. Lazy resident/Sentinel purse creation is a
once-only endowment keyed to its persistent purse identity; respawn binding
reuses that identity. Disconnected wallets retire in the journal and remain
in aggregate totals. No reconnect policy changed.

Bank transaction rules are implemented in the domain: deposit100 burns2 and
credits98, withdrawal30 leaves68, exact balance revisions are required, and
negative/zero/no-credit/overflow/insufficient amounts reject. There is no bank
RPC or UI yet. The bank builder is not a physical access check and must only be
used behind the Treasury's alive/unprotected/range/sight/quiet/session gates.
It does not grant remote banking or modify ordinary wallet/theft/death policy.

Integer market pricing is implemented as reusable rules for the next B62 slice.
It applies the event ratio before fixed base-price clamps and uses the same
next-stock expression for bid. Quote/session/cash/stock serialization and
physical market transfers remain B62 work.

## Verification

- Editor build **passed** on pinned UE5.8.4 CL58902052, MSVC14.44.35229,
  SDK10.0.26100.0, MaxParallelActions4: ten actions, 36.47 seconds.
  Evidence: `Artifacts/20261023-134341-028-BuildEditor/BuildEditor.log`.
- The first build identified a shadowed catalog member and incorrect pointer
  deduction over TObjectPtr; both were fixed before the successful build.
  Failed build: `Artifacts/20261023-134018-127-BuildEditor/BuildEditor.log`.
- One bounded native rule launch **timed out at 60 seconds after editor startup
  with the suite queued, before any test began or produced a result**. No rule pass, including the two new tests or
  CargoCapacity, is claimed. No retry or timeout extension was performed.
  Evidence: `Artifacts/20261023-134441-220-Automation/Automation.log`.
- `git diff --check` passed; runtime/specification JSON contents match.
`python docs/v06/tools/check_model.py` passed again: 229 configured quote states
and the supplied budget/bank/cargo/window arithmetic. This is specification
arithmetic, not native gameplay evidence.

Added native EconomyAccounting and EconomyCatalog rule tests cover replay
identity, bank arithmetic/stale rejection, finite reward/node exhaustion,
failed-transaction atomicity, causal transformation, lazy endowment,
retirement/reconciliation, catalog validation and market cycles. They compiled successfully but did not execute in the bounded launch.
Pricing tests cover 216 ordinary stock states and shortage-ratio cycles; the
Python specification model counts 229 configured states including the thirteen
additional East Toolkit shortage states.

No new Game build, cook/package, network session, physical-bank scenario,
production journey, human review or long stress test is claimed. Previous
CargoCapacity startup timeout remains historical evidence, not a passing test.

## Subsequent slice

[Physical Treasury banking](B61_PHYSICAL_BANKING.md) now integrates the domain
with native review/confirm, persistent owner BankGold and physical authority
sessions. The earlier absent-bank statements above describe this historical
accounting slice. See the banking report for the latest compiled state.

## Remaining implementation at this checkpoint

Finish physical bank access and private owner view; integrate all retained
book/ward/Seal/cargo transitions into causal item accounting and compare the
physical world with the journal (current replay proves the domain records,
not complete physical-world conservation). Persist/export named match-close
and endowment evidence. Bind the rest of runtime recipes/budgets/services and
implement B62 commerce, B63 production/skills, B64 schedules/geography, B65
distinct endorsements/public Charter, B66 native UI, then scoped B67 acceptance.
Do not promote E31/E32/E33/E41/E42 to complete based only on these foundations.
