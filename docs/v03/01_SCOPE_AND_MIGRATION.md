# Scope and migration policy

## Why this release exists

The immediate problem is that presentation and UX prevent serious evaluation of a promising prototype. v0.3 should establish a usable Unreal foundation and remove platform uncertainty. It should not simultaneously redesign victory, add a roster, invent new items or require full production art.

Use a true 3D scene, collision and navigable elevation with a controlled top down camera. Gameplay remains on the ground surface, including ramps, without jumping, flight, free camera aiming or vertical combat. This is the proposed 2.5D interpretation of the user's request. It is not a requirement for first person play or a fully free 3D action game.

## Platform decision

| Area | v0.2 | v0.3 decision |
| --- | --- | --- |
| Delivery | Desktop browser | Native Windows desktop first |
| Rendering | Phaser 2D | Unreal 3D scene and top down camera |
| Runtime | TypeScript and Node.js | Unreal C++ with focused Blueprint presentation |
| Multiplayer | Colyseus and custom web snapshots | Unreal transport and character movement, with deliberately filtered owner views |
| UI | Browser HUD and controls | Native UMG widgets and Enhanced Input |
| World | Pixel geometry, height cells | Centimetre geometry, collision, ramps and explicit vision levels |
| Builds | Web dependency and bundle pipeline | Pinned engine, C++ toolchain, editor build, cook, stage and package |
| Reference value | Playable prototype and its data | Preserve for behavior, timing and side-by-side review |

There is no hybrid browser client, Colyseus-to-Unreal gateway, shared live match, Pixel Streaming or web deployment requirement. Extract useful tuning and scenarios; do not translate each old subsystem mechanically into C++.

## Audit the completed prototype

The user reports v0.2 complete. Treat that as project context, then inspect the actual repository to discover implementation differences. Record revision, startup commands, configuration, mechanics, assets, tests, known defects and screenshots or recordings. Do not assert that the code matches every old proposed number.

If the repository is absent, finish the proposed Unreal scaffold that does not depend on its internals, mark source comparison pending and report the precise missing input. Do not delete the prior build or replace it with an unverified rewrite. A gameplay mismatch must be recorded rather than silently called a platform change.

## Mandatory scaffold coverage

1. Valid Unreal project, version record, runtime and editor modules, source control conventions and repeatable asset bootstrap.
2. Native host and direct-address join, authoritative movement and combat, ownership validation, death and a scheduled return in independent client processes.
3. 3D diagnostic map with flat public space, wild cover, trees, a raised platform, ramps, a small crowd and public and private sight sources.
4. Usable camera, cursor aiming, keyboard controls, interaction prompts, native panels, progress, rejection feedback and readable self status.
5. One real courier collect-and-deliver transaction and one real ten second Heart claim with contest and interruption.
6. Unit and map-level tests, filtered-view tests, a packaged Windows build and an explicit feature migration ledger.

## Scope classification

| Feature | Required v0.3 coverage | Full parity status |
| --- | --- | --- |
| Movement, sprint, collision, aim | Real implementation | Retain behavior after unit conversion |
| Basic attack and dash | Real networked implementation | Preserve baseline timing and no wall traversal |
| Assassination | Real warned attempt and turn, range and damage interruption | Preserve the single shared kit; no ability framework expansion |
| Death and return | Real persistent contender and new life body | Test original delay formula, retained state and protection |
| Fog, height, walls and trees | Real authority and renderer path | Small map first, not full city layout parity |
| Public sensors and private ward | Real visibility sources and loss | Tower combat and complete ward shop economy deferred |
| Courier profession | One real collection and paid delivery | Six-job rank progression, full crafting and profession balance deferred |
| Heart | Real access check, ten second claim, contest and single result | Ordinary defensive progression and full profession route use fixtures in this release |
| Concord Seal | Real carry, drop and pickup transaction | Crafting acquisition provided by a labelled fixture |
| Residents | Eight legal wandering, nonviolent NPCs with role-neutral presentation | 24 residents and richer city routines deferred |
| Autonomous contenders and wild camps | Define interfaces and ledger entries | Goals, preparation, leash and camp port deferred |
| Items, fountains and upgrades | Data schema and audited port entries | Full interactions and economy deferred except explicit slice transactions |

An interface without behavior is a scaffold, not a ported feature. Do not show a fixture-seeded Seal win as evidence that the full peaceful progression loop has been migrated. Do not call a public sensor toggle tower destruction parity.

## Exclusions

No new character roster, squads, online service integration, ranked matchmaking, persistent accounts, public deployment, store integration, production dedicated hosting, generic ability framework, full Gameplay Ability System migration, Lyra framework adoption, procedural city, destructible terrain, final art or full v0.2 population stress certification. These can follow after the foundation passes.

## Ledger and parity rules

Fill the bundled migration ledger from code inspection. Status values are Not started, Ported, Replaced, Fixture only, Deferred or Deviated. Record actual baseline behavior, Unreal implementation location, test evidence and any semantic change. A ported entry requires a real path through the native client and authority, not a local widget simulation.

Keep raw gameplay time, damage, HP, costs and thresholds unchanged unless the actual baseline differs or a measured issue requires a recorded decision. Distances and speed are converted explicitly. Asset quality and interaction feedback can improve without changing resource or victory rules.

The v0.3 completion review evaluates the scaffold and its slice. Full v0.2 regression parity is a later checkpoint listed in the ledger, not an implied prerequisite hidden behind the word migration.
