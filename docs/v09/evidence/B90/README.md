# B90 portable evidence

These are selected native outputs from the tested working tree based on
ebc19a5. Audit.json pins code, generated assets, commands and local full-log
directories. Source commit metadata is recorded after the implementation commit.

- NativePaths.json: all 120 fitted pads and native path/collision/grade counts.
- NativeTravel.csv: all 2,559 native queries; times are 420 cm/s estimates.
- TravelComparison.csv: 20 prescribed trips, graph/native estimates separated;
  only the antechamber-to-Heart row has actual walked time.
- FittedGeometry.txt: canonical saved actor and instanced-presentation transforms.
- generation_a / generation_b: independently regenerated native outputs, with
  identical semantic transforms. Final material refresh preserves those transforms.
- walk / bridge_entry: actual CharacterMovement after labelled start placement,
  with no ability or speed grants. The bridge run corroborates the one
  conservative capsule edge candidate; it is not silently counted as a clean sweep.
- NativeRules.json, BuildEditor.log and BuildGame.log: scoped engineering checks.
- v08_sources and V08Sources.log: isolated inherited profile source/accounting
  fixture, not a v0.9 earned-progression demonstration.
- Capital.png / Render.log: offscreen 1280×720 placeholder structure at a
  diagnostic camera distance; no human readability or final-art claim.

Replay from the repository root:

```powershell
python -B Scripts/CheckB90Evidence.py docs/v09/evidence/B90 --walk docs/v09/evidence/B90/walk --bridge-entry docs/v09/evidence/B90/bridge_entry --compare docs/v09/evidence/B90/generation_a
python -B Scripts/CheckEconomyEvidence.py docs/v09/evidence/B90/v08_sources/Ledger.json
```

The B90 release gate remains partial. Full-map Wet/frozen gameplay, services,
accounting/reset, border push/dash scenarios, long walked/empty-interval traces,
multiplayer mutation/privacy, AI, long sessions and human pacing are not proven
by these outputs. See the implementation report and working ledger.
