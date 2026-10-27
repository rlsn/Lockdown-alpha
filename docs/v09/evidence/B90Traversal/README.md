# B90 native traversal evidence

Source: b96c329. All inputs and generated map geometry remain those of the
previous B90 foundation. Audit.json records exact commands, toolchain, source
hashes and local artifact directories.

- Traversal.json: 31 journey receipts, 15 boundary receipts, navigation contract,
  observed timestep and scope.
- TraversalTrace.csv: actual per-second positions, speeds, distance and nearest
  POI geometry; no hidden or invented interaction rewards.
- BoundaryTrace.csv: per-tick and per-push-step safety samples.
- MeasuredTravel.csv: graph estimates, native paths and actual walked times.
- TraversalSummary.json: independent replay result.
- BuildEditor.log / BuildGame.log / NativeRules.json: build and 49-rule results.
- heart / bridge_entry: retained short-walk regression results under the updated
  controller.

Replay:

```powershell
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Traversal
```

The cumulative 6,149.867 seconds are separate fixture journeys with diagnostic
starting placements and accelerated wall execution. They do not certify a full
competitive match. Boundary push tests native floor/capsule geometry, not the
competitive action dispatcher or costs. Static water geometry is not a Wet
status test. POI gaps are a placement proxy, not human pacing evidence.
