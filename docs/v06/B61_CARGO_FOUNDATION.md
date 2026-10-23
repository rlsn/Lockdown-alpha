# B61 first slice — physical goods and cargo limits

23 October 2026. B61 is **partial**. Goods/cargo source integration compiles;
persistent banking, institutional records, transaction receipts and aggregate
reconciliation are not implemented. B62–B67 have not started. This checkpoint
does not provide a playable v0.6 economy or certify E05/E31/E32/E33/E43.

## Implemented

Append Fiber, Ore, Crystal, Toolkit and Core to the existing physical item enum,
preserving prior Book/WardCharge/Seal values. Each retains the common unique ID,
holder, slot, availability revision and reservation flag. Server-only origin
receipt/parent-ID fields provide lineage storage; they are **not yet populated
by a causal ledger** and do not enter owner inventory or peer offer payloads.
No commodities are granted, sold or gathered yet.

`LDInventory::Fits` projects complete final slots, mass and legacy one-Seal
count after exact outgoing IDs and incoming kinds. It rejects unknown weights,
duplicate/foreign outgoing IDs, full slots and overweight results. Weights are
the adopted 1/3/2/4/6 for goods, book/ward 1 and legacy Seal 2. Capacity is
60+2*(STR-10), 56–72 for valid STR 8–16. These are currently native constants;
versioned runtime catalog/bootstrap integration remains B61 work.

Live pickup/adoption, vendor purchases and both sides of peer exchange use the
same projection before committing. Preparation checks the candidate build's
capacity both at channel start and final commit, without spending on rejection.
Trade still validates ownership/revisions/reservations and both consents; reserved
items retain their existing carried mass. No movement formula changes.

Commodity death/disconnect handling releases reservations through the existing
life path, drops the same physical instance on legal ground and clears owned
quick references. It does not consume cargo or attach its lifetime to corpses.
Books/legacy Seal keep their existing separate drop paths, and unused wards keep
their loss-on-death policy. Compatibility projection no longer mistakes any
non-book/non-ward item for a Seal.

Filtered observation adds a generic Cargo kind within the existing eight fields,
without canonical item IDs, parents or hidden ownership. Body-sight pickup uses
the ordinary temporary observation capability and 80 cm validation; approach
stops within 64 cm. The bag shows slot/weight totals, a Cargo filter and cargo
detail. Materials cannot bind to shortcuts or trigger book/ward Use; the server
also rejects those operations. The existing vendor shows a precise cargo-capacity
reason. Layout/input/death/remote execution has not been newly certified.

Supplied discussion text now has `-text` attributes to preserve the published
byte hashes through Git checkout. This corrects initial Git newline normalization
of the newly imported source copies; their original working-file bytes are kept.

## Verification

- Incremental Editor build **passed**, UE 5.8.4 CL58902052, MSVC14.44.35229,
  SDK10.0.26100.0, MaxParallelActions4: 23 actions / 304.62 seconds total.
  Evidence: `Artifacts/20261023-102029-314-BuildEditor/BuildEditor.log`.
- Added `Lockdown.Rules.CargoCapacity` for exact boundary, net exchange/craft,
  overcapacity preparation, reservation mass, unknown/duplicate IDs, quick-use
  exclusion and observed-cargo serialization cases. It compiled, but execution
  is **not run**: the bounded 60-second rule launch timed out during editor-plugin
  loading, before tests started. No automation report or pass is claimed.
  Evidence: `Artifacts/20261023-131741-354-Automation/Automation.log`.
- B60's Python specification model passed 229 price states and arithmetic. It
  does not exercise this C++ implementation. Source/diff integrity checks are
  recorded with the commit, without substituting for native execution.

No retry with a longer timeout, Game build, cook/package, gameplay probe,
network session, OS-scale review, restart loop or long session was run. Existing
B57 package/evidence describes the old runtime and is not evidence for cargo.
The user requested minimal checks before continuing the next version.

## Next

Finish B61 persistent bank/institution/node/budget identities and one-time match
initialization, complete causal gold/item deltas and reconciliation, including
lazy NPC purses and retired disconnected-wallet accounts from B60. Integrate the
versioned runtime data and then populate item origin/parent metadata. B62 supply
and market transactions and B63 production must use the final `Fits` projection
and shared physical reservation authority. Retain the locked-roster policy.
When native checks are next run, obtain an actual CargoCapacity result; do not
promote this timed-out launch to a passing acceptance gate.
