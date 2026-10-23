# B58 limited-verification handoff

23 October 2026. Runtime baseline: `bd86a85e440e5699bbe91627d696296cd6518e9b`
(B57). The user directed: “avoid lengthy tests, keep verifications minimal” and
said the project is moving onto the next version. This checkpoint records the
v0.5 engineering handoff using existing evidence. It does not certify every
v0.5 release gate or define the next version's features.

## What is delivered

B51–B57 implement native navigation, saved input mappings and explicit targeting,
masked inspection and own HUD/minimap, the 24-slot physical bag and four quick
references, quoted Guild purchases, consensual atomic peer trade and shared
presentation with panel focus and final review. The ordinary frontend uses V05.

The current warm Win64 Development package is:
`Artifacts/20261022-220153-009-PackageWin64/Archive/Windows`.
Run [OpenPackaged.ps1](../../Scripts/OpenPackaged.ps1) with that directory
as `-PackageRoot`; launch/host/join instructions remain in the project README.
The [B57 report](B57_IMPLEMENTATION.md) records the source/payload provenance,
actual screenshots, fixture limits, corrections and measured transport.
The [archived continuation](CONTINUATION_B57.md) preserves the detailed checkpoint.

## Minimal checks performed for this handoff

No engine build, cook, gameplay probe, restart loop, soak or user session was run.
The repository was clean at entry. The existing package audit was rerun against
the B57 runtime commit: all 145 Source/Config files and seven runtime payloads
match, with no missing or mismatched hashes. Existing report JSON was read and
checked for failure/incomplete results; this is retained evidence, not new test
execution. Documentation links, ledger structure and change scope were checked.

| Retained evidence | Result | Source report |
| --- | --- | --- |
| Rule automation | 19 succeeded; zero failed, incomplete or unrun | `Artifacts/20261022-222738-262-Automation/Report/index.json` |
| Trade versus death | 43 / 17 / 12 | `Artifacts/20261022-221912-733-Packaged-Trade-150-25-Death-UI1280x720-125pct` |
| Trade versus disconnect | 41 / 17 / 11 | `Artifacts/20261022-222123-290-Packaged-Trade-150-25-Disconnect-UI2560x1440` |
| Trade versus terminal state | 42 / 17 / 12; actual RTT 99.49 / 99.40 ms | `Artifacts/20261022-224004-247-Packaged-Trade-100-10-Terminal-UI1920x1080` |
| Vendor | 23 / 19 / 14 | `Artifacts/20261022-222228-388-Packaged-Vendor-150-25` |
| Inventory | 59 / 25 / 25 | `Artifacts/20261022-222517-641-Packaged-Inventory-150-25` |
| HUD/input | 26 / 37 / 37 | `Artifacts/20261022-222622-313-Packaged-HUD-150-25` |

Counts are authority / owning client A / owning client B. The B57 report distinguishes
nominal delay settings from actual echoes, rendered A viewports from headless B,
and labelled engineering fixtures from normal earned progression. Earlier B51–B56
reports retain their own navigation, privacy, earned-purchase and authority proof.

## Carried-forward acceptance and deviations

| Gate | Remaining work and evidence boundary |
| --- | --- |
| U46–U47 | Complete on-screen Windows 100%/150% scaling, pointer/physical-keyboard and user state review. Offscreen captures and a queried 100% desktop are not this acceptance. |
| U49 | Current cold checkout/build/cook and delivery verification remain unrun. Existing package/build results are warm; dedicated-server packaging and remote Git/LFS delivery remain conditional/unverified. |
| U51–U52 | Current ten restarts, normal 25-minute lifetime and measured UI/path/frame/traffic comparison remain unrun. Extended runs are deferred under the user's minimal-verification direction. The separately deferred 90-minute stress remains deferred. |
| U53–U54 | Retain earlier normal peaceful/character evidence and B55 earned purchase/read. The complete current v0.5 journey through the new UI was not newly demonstrated; trade probes use explicit fixtures. |
| U55 | No uncoached participants, timings, assistance counts or human multiplayer acceptance are claimed. |
| U56 | Handoff, ledger and existing evidence are recorded; full release acceptance remains partial. |

Known presentation differences: 1080p larger text uses a 250-logical-unit HUD band
instead of the proposed 220; a long dead-overlay clock cue can wrap beyond its
background at 720p/125% while remaining readable; trade review shows exact outgoing
and incoming terms without a separate projected-wallet arithmetic line.

Keep Unreal 5.8.4 CL 58902052, filtered views, four authoritative shared skill
slots, real costs and holds, finite supplies, unescrowed trade gold, both stages
of consent and atomic conservation. Frozen master/specification/data/wireframes
are unchanged. Full conquest, extra economy systems, controller/4K/ultra-wide,
screen-reader integration and full keyboard world play are not implied delivered.

## Next version

No next-version specification was found in `/docs` during this handoff. Its version
identifier, scope and acceptance must come from the user's next specification;
no v0.6 feature plan or runtime changes are invented here. Start from B57's runtime
and this handoff, carrying the open gates above into the next plan. Keep subsequent
verification short and targeted unless the user changes that constraint or a
concrete regression requires a focused check. Do not automatically restart the
deferred long runs merely because the version changes.
