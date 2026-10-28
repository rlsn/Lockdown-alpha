# B91 — settled environment checkpoints

28 October 2026 · native source `2957b43cb1639d993fd315f61191367a6078b4ca` · B91 remains in progress

An explicit server-local checkpoint now preserves settled environmental mutations
across a process restart. This closes a bounded part of B91's saved-state work.
It does not resume contenders, inventories held by players, economic accounts,
rosters, live fights or the complete match.

## State and lifecycle

The file contains every authored source address and kind, fitted/current position,
clear placement, revision, HP, exhaustion and harvest/burn receipt. It retains the
single harvest reward identity, exact physical item IDs and provenance, ground or
container location, storage references, consume tombstones, door flags/revisions,
remaining tool fuel and remaining instance cooldowns. A destroyed or consumed
source cannot regenerate its goods when restored. Dynamic dropped-item wrappers
receive stable addresses derived from their physical identity.

Relative timers pause while the server is offline. A lit tool resumes spending its
remaining fuel after restoration. Checkpoints are trusted operator-selected files;
the checksum detects corruption, and does not authenticate files or prevent an
operator from selecting an older checkpoint. This is within-match recovery, with
no cross-match progression or retained competitive power.

Saving requires an authoritative, ready `v09_environment` world with no connected
contender controller. Pending channels, cargo, traversal returns, deployed portable
links, projectiles and temporary fields must settle first. Personal inventories,
reserved/bound or derived goods and unsupported state refuse the operation. A
refused save leaves the previous file intact. Successful writes stage a separate
temporary file and replace the destination only after validation.

Loading requires a pristine, ownerless world and can run once. The complete file
is checked before live actors change: schema and size bounds, profile/contracts,
checksum, source census and kinds, legal state combinations, finite endowment
counts, provenance, reward references, one location per physical item, storage
weight, fuel bounds, door legality and supporting placement. Allocations are
staged before original goods are removed. Restored actors rebuild the same
collision/sight/navigation revisions; observer handles are never saved.

## Operator entry points

Launch the dedicated B91 environment with `-LDB91Restore="<server-local-file>"`
to restore before admitting contenders. An invalid startup restore refuses the
environment rather than silently starting fresh. Without that flag, a new process
starts the original fresh environment.

`LD.B91.Checkpoint <server-local-file>` saves through the server console. Optional
`-LDB91Checkpoint="<server-local-file>"` checks every two seconds and writes changed
state only while the ownerless environment is settled. These are server operator
controls. They add no player interaction key, client RPC or UI command.

## Evidence and limits

Run `Scripts/RunB91Checkpoint.ps1`. Four sequential native dedicated-server
processes write mutations, restore at startup, reject adversarial files without
changing the pristine world, and start fresh without restore flags. The fixture
compares every source, item and door record, including one permitted harvest
yield, one consumed Water Flask, partial and destroyed saplings, burnt grass,
stored lit Torch fuel and a broken-open rear door. A second restore is refused.
All 53 native checks pass, including preservation of remaining cooldowns and
refusal of a door revision newer than the saved global mutation revision.

The companion ignition fixture initially inspected Oil-to-fire and capacity state immediately
after the success receipt, before the admitted combat transition had necessarily
committed. Its observation now waits for the pending transition queue to drain.
Both initial failed observations are retained alongside the final regression rather
than counted as acceptance. This adjustment changes the fixture, not gameplay.

| Verification | Result |
| --- | --- |
| Four-process settled checkpoint | 53 native checks |
| Two-owner grass | 44 native checks |
| Source/pointer controls and finite saplings | 47 native checks |
| Paid Torch ignition and reserved fields | 42 native checks |
| Storage/cargo/furniture | 51 native checks |
| Native rules | 55 tests |
| Full-unity Editor and Game targets | Both pass |
| Frozen asset / supplied-file hashes | 148 / 59 unchanged |

Grass and source/pointer regressions precede the ignition fixture observation
fix. Final ignition, storage, rules and both native builds include that fix;
the checkpoint gameplay implementation is the same throughout these final runs.

Mutation setup is explicitly diagnostic. The companion source/pointer, grass,
storage and ignition network fixtures cover player-earned transactions separately.
Neither type of evidence proves full-match resumption or human recovery usability.
See [frozen evidence](evidence/B91Checkpoint/README.md) for source and raw results.

Full contender/session/account identity, action reconciliation, roster and economy
resume remain open. The complete reference encounter, 3,000 scenery trees, broader
paid spell/reaction paths, layered fields, density and human acceptance remain B91
or later-stage work. No whole-stage completion is claimed.
