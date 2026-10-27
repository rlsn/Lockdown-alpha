# B90 river-contact evidence

See [implementation and limits](../../B90_RIVER_CONTACT.md). `Audit.json` pins
the native source, current saved assets and retained evidence bytes. Previous
B90 and B90Traversal evidence remains immutable and refers to fitter revision 1.

- `river/`: 55 actual native contact journeys and per-tick traces. All seven
  named crossings and all 17 route/river intersections are covered; the Python
  checker also reconciles these intersections with the supplied authored list.
- `rendered/`: separate frozen Heart-inset walk, native HUD capture and trace.
  Cyan outlines are diagnostic geometry, not final ice art.
- `map/`: fitter revision 2 build/reopen path and ground evidence. The canonical
  pad/actor/instance manifests match after reopening with the original unchanged
  terrain and material assets. `map/second/` retains a second independent fitter
  revision 2 generation with identical canonical pads and actor/instance transforms.
  The exact first-generation assets used by movement tests are retained.
- `traversal/`: complete fitter revision 2 replay of 31 journeys and 15 physical
  boundary cases. This is aggregate fixture walking, not a competitive session.
- `legacy/`: v0.8 B82 server/two-client river regression with 100 ms RTT and
  ±10 ms half-lag jitter configuration.
- Native rules and build logs retain the corresponding focused validation.

Recheck the portable traces from the repository root:

~~~powershell
python -B Scripts/CheckB90River.py docs/v09/evidence/B90River/river
python -B Scripts/CheckB90River.py docs/v09/evidence/B90River/rendered
python -B Scripts/CheckB90Traversal.py docs/v09/evidence/B90River/traversal
python -B Scripts/CheckB90Evidence.py docs/v09/evidence/B90River/map --compare docs/v09/evidence/B90River/map/second --bridge-entry docs/v09/evidence/B90River/bridge_entry
~~~

The fixtures use starting placements and explicit frozen-field seeding. Ordinary
movement during each journey uses native CharacterMovement and navigation, with
no paid ability or item dispatch claimed. No full-map multiplayer, privacy,
economy, resource-capacity stress or human pacing acceptance is implied.
