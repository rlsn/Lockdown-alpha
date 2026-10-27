# B90 approach inspection evidence

See [implementation, metrics and limits](../../B90_APPROACHES.md).
`Audit.json` pins source, the unchanged compiled payload, 148 unchanged map
assets and all retained evidence bytes.

- `full/`: all 23 incoming final-section walks, seven-target native path/pair
  report, 144 shell calibrations, range/deck checks and independent summaries.
- `rendered/`: separate antechamber-to-Heart walk with F5 overview and native
  screenshot. Its report contains all static branches; only one walk is run.
- `bounds/`: compatibility replay of all 15 existing boundary cases.
- `ordinary_route/`: original Routes-lane antechamber-to-Heart regression.
- Build and native-rule logs record focused validation on pinned Unreal 5.8.4.

Recheck portable reports from the repository root:

~~~powershell
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Approaches/full
python -B Scripts/CheckB90Approaches.py docs/v09/evidence/B90Approaches/full
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Approaches/rendered
python -B Scripts/CheckB90Approaches.py docs/v09/evidence/B90Approaches/rendered
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Approaches/bounds
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90Approaches/ordinary_route
~~~

The complete run finds 33 spatially distinct pairs and three merged pairs. No
sampled in-range cross-branch ray is geometry blocked: 129 are clear and 159
fail radius before tracing geometry.
Range failure is not cover. Calibrated walls/decks do block rays. Static helper
results do not authorize unit rendering, inspection, targeting or replication.
Tagged ledges, smoke, concealment, mutations and restricted-client sight remain
B91 work. These separately placed fixtures are not a competitive match or human
pacing observation. Historical evidence is retained unchanged.
