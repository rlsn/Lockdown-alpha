# B90 world-skeleton exit evidence

This bundle records the twelve **stage-owned spatial** cases. See
[the completion report](../../B90_COMPLETION.md), [StageSummary.json](StageSummary.json)
and [Audit.json](Audit.json). Full gameplay acceptance remains false; the audit
records B91–B94 dependencies and exact native source/asset hashes.

| Directory/file | Evidence |
| --- | --- |
| `structure/` | 96 native addresses, 36 service/building seats, 11 identities, 16 aliases; 216 doorway capsule lanes, 108 closed-shell rejections, 72 actual walks and road/wall overlap scan |
| `closure/` | Three real full-map reloads, stale-generation rejection, empty fresh fields and dead old actor handles; same-process v08 source report/ledger in `legacy/` |
| `map/` | Retained saved-map reopen; `first/` and `second/` independent generations; 2,559 paths, ground diagnostics and canonical actor/instance transforms |
| `bridge_entry/` | Actual CharacterMovement corroboration of one conservative eastern bridge edge contact |
| `traversal/` | 31 walked trips, 15 movement boundaries, raw traces, measured/estimated tables and elapsed-time-weighted heat PNG/SVG |
| `river/` | 55 native thawed/frozen/deck/contact cases and per-sample trace |
| `approaches/` | Seven targets, 23 actual local walks, 36 branch comparisons and classified static rays |
| `captures/` | Native 1600×900 F6 capital, Riverport and Oakmere overviews; static structure checks, explicitly zero capture-lane walks |
| `NativeRules.json`, `Automation.log` | 51 native rule tests, zero failures |
| `BuildEditor.log`, `BuildGame.log` | Successful Win64 Development builds |

All native operations ran sequentially. Long movement/contact/structure fixtures
preceded only final HUD framing and settlement camera zoom adjustments; their
movement, binding, contact and fitting code did not change afterward. Final
captures, map rebuild/reopen, game build and rule run use the final native source.
The audit pins this distinction instead of claiming every earlier executable
contained the final UI. The retained first map/definition packages are the exact
bytes used by the fixtures; the other 146 generated packages are unchanged.

Run from the repository root:

~~~powershell
python -B Scripts/CheckB90Completion.py docs/v09/evidence/B90Completion
~~~

This check recomputes derived summaries and checks the raw evidence against the
current compiled payload. For byte-level provenance, compare the audit's
`files_sha256`, `generated_assets_sha256` and normalized source hashes separately.
Do not use the current-payload checker to rewrite historical fitter1/fitter2
bundles.

The heat figure aggregates fixture seconds in 50 m cells; it is not a population
traffic forecast. The longest geometric POI-anchor gap is 339 seconds. Inspected
in-range approach rays have no geometry cover. Those findings remain B91 fitting
risks. Declared service functions and civic policy rings do not implement live
transactions, guards, sight authorization or protection.
