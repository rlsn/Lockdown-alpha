# B91 — checkpoint placement validation

29 October 2026 · native source `ec529a3c845df2a6562da59b193d4a86e12d8d57` · B91 remains in progress

Checkpoint restoration now validates heavy-prop footprints against the proposed
saved scene. Previously, a center-ground check with an80cm tolerance could admit
floating cargo, a mismatched clear pad, overlapping saved crates or intersection
with fixed world geometry. Recomputed checks run before any live actor changes.

The saved clear pad must agree with the actor pose. Every footprint corner must
have legal support and grade; the prop rests above the highest sampled support.
Collision checks ignore the pristine runtime props that restoration will replace,
then compare the saved solid poses. This avoids rejecting legitimate relocation
because an obsolete starting pose still occupies the old scene. Existing reserved
route, doorway, stair, spawn, service and Heart-pad checks remain in force.

The four-process checkpoint fixture now relocates one existing crate to a legal
pad and compares its exact pose across restart. Checksummed negative files attempt
floating cargo, inconsistent clear pads and overlapping crates; a temporary
collision fixture checks intersection with fixed geometry. Each failed restore
must preserve the pristine world's fingerprint, and valid restoration must still
succeed afterward. These are diagnostic persistence fixtures, not a new earned
player cargo journey. See [evidence](evidence/B91Placement/README.md).

The local engine's optional Fab plugin references unavailable FabToolset module
rules. The Editor target excludes Fab, and the checkpoint runner disables it at
launch. Project plugin preferences and engine files are unchanged. This does not
change game assets or item acquisition. The failed build attempts remain in local
Artifacts; successful Editor/Game build logs are frozen with this increment.

Full contender/economy recovery, the earned same-source saved-match journey,
remaining paid reactions, scenery trees, reference encounter and human/density
acceptance remain open. No whole-B91 completion is claimed.

Verification:59 native checks across four sequential processes; full-unity Editor
and Game builds pass. All148 protected assets and59 supplied files retain their
original bytes. The historical53-check evidence still passes its independent
checker; this is not a new native run of the historical source.
