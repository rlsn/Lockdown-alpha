# B90 baseline review and adoption

27 October 2026. Inspected source baseline: `ebc19a5` on `main`.

## Repository and source status

The active repository is the Windows Unreal project at `D:\Unreal Projects\Lockdown`.
The separate `D:\Workplace\lockdown` repository is the retained v0.2 web prototype
at `74d3d3e`; it was inspected for identity only and remains unchanged. No AGENTS.md
was found in the active repository or its inspected parent locations. The only
initial active-repository change was the untracked `docs/v09/` specification.
Its adopted file hashes are recorded in `SOURCE_MANIFEST.json`.

Unreal is pinned to **5.8.4 CL 58902052**. Current native builds use Win64
Development, MSVC 14.44.35229 and Windows SDK 10.0.26100.0. Existing build,
bootstrap, automation and probe scripts resolve this pin through Common.ps1.
The installed binary engine does not imply a packaged dedicated Server target.

## Delivered native baseline

The code and B86 report agree on the schema8 `v08_integration` profile: six mixed
action slots, seven body/four accessory bindings, nine presets and one intrinsic
trait, all 52 abilities/80 items/20 recipes, paid acquisition and four earned
Heart routes. B86 has source-aware native UMG/Enhanced Input/Slate behavior and
a packaged four-client workload. Its human/long-session/operating deferrals and
reported service-cost/rendering limits remain in force. The earlier docs index
still said B84/B85-next; that is stale navigation, not the source baseline.

Movement uses ALDCharacter and native CharacterMovement with a 28 cm radius,
90 cm half-height capsule. Tactical walking is 420 cm/s. The existing navigation
component owns authoritative match/life/order movement intent. Combat, vision,
inventory, economic accounting, procurement, banks, physical acquisition and
route leases already have native authorities. The implementation does not use
GAS and requires no framework migration.

The region builder creates saved mesh geometry, collision channels, cooked nav
bounds and Recast data. However, ULDRegionDefinition supports only 16 nodes,
32 routes and a small public grid. FLDVisionGrid is fixed to 60×40 cells, and
serialized small-map assumptions are spread across view, minimap, navigation and
environment services. Simply scaling that definition would violate the 2.4 km
coordinates and observation contract. B90 therefore adds an independent spatial
definition and isolated native builder/preview. It does not expand legacy wire
bounds or run an unchanged short-map economy at full-map coordinates.

## Starter and Witness audit

The schema8 starter wallet is zero and inventory initialization creates no free
gear. ALDGameMode sets schema8 starting WardCharges to zero; inventory initializes
eleven bindings and zero historical Quick references. TacticalStart preserves
the three legacy starters plus six catalog teaching kits, natural distributions
and starting Rank1 skills. B90 does not alter any of these definitions.

Witness exists in ALDWorldChecks::UseCharter. Four public alternatives map to
Heave, Analyze, Parley and Survey. The skill must be equipped, check score must
reach 12, and the player must be active, unprotected, in range and able to see
the service with clear geometry. A below-threshold attempt rejects before
cooldown. Successful ordinary skill commit creates one contender Witness GUID
with no XP/gold. SkillValue uses the corresponding effective attribute plus
2 per additional rank (and the retained applicable subclass adjustment).
The service identity and its small-map positions still need explicit B92
full-map relocation/integration. No free Witness substitute is necessary or
authorized by this audit.

## Adoption and scope decisions

- Preserve supplied v0.9 prose, manifests, templates, atlas, source archive and
  initial acceptance statuses. Add implementation reports and a separate ledger.
- Compile only B90 spatial data into staged Content, with per-input SHA256 and
  a closed native checksum. Full-profile economy/population/object verbs remain
  later implementation; the compiler is not a fake implementation of them.
- Keep v08_integration catalogs, generated map, economy and profile selection
  unchanged. B90's map uses a standalone spatial game mode, visibly labelled as
  a preview. Full gameplay admission remains dependent on B91–B93.
- Native saved collision/navigation evidence is distinct from graph/model checks.
  Path time estimates are not walked time. The short Heart walk uses a labelled
  initial placement followed by ordinary native movement, without abilities.
- Broad B90 acceptance includes some contact/service/claim behaviors assigned
  to later stages. Record these dependencies explicitly instead of claiming a
  structural preview can satisfy those gameplay cases.

See [implementation/evidence](B90_IMPLEMENTATION.md) for actual outcomes and
remaining gates. `docs/master` revision 8 is a living design reference; frozen
revision 7 remains in historical source archives/reference copies.
