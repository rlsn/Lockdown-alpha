# Project Lockdown v0 3 Unreal scaffold request

Build the Unreal migration scaffold described in this package. The user has completed the web-based v0.2 prototype, considers its gameplay promising, and has chosen Unreal to improve presentation and testability with a 3D world and top down 2.5D play. Implement a working native foundation and the defined slice, not only a plan, static mockup or empty project.

Read the package README and documents 01–09, the migration ledger template, the current master plan and the actual v0.2 repository if available. Frozen v0.2 references establish gameplay intent, but this package controls native architecture and release scope. Do not reopen the engine comparison or expand the game design.

## First inspect and pin

Audit the existing prototype, available Unreal engine and Windows C++ toolchain. Record exact engine patch, distribution, compiler, SDK, paths, baseline revision and actual configuration. Select a supported installed non-preview engine rather than assuming a latest version. Keep the prior web project intact. Record missing dependencies without claiming a build occurred.

Create a code-first `ProjectLockdown` Unreal project unless a compatible project already exists. Keep runtime and editor modules separate. Use C++ for authority and testable rules, with Blueprint and UMG for focused presentation. Pin plugins and create repeatable preflight, build, asset-bootstrap, test, local-network and Win64 package scripts. Save actual Unreal assets through Unreal; never fake `.uasset` or `.umap` files.

## Required foundation and slice

Implement a 3D migration-demo map, human-sized shared placeholders, actual ramps, trees and walls, a top down camera, cursor ground aiming, native controls and clear native prompts and progress. Use the package's proposed two-centimetres-per-pixel conversion and preserve gameplay time and damage. Ordinary movement uses Character Movement, not a second ported web integrator.

Provide native Host demo and Join address UI. Run independent clients through server-owned movement, basic attack, compatible dash, warned assassination, HP, death and scheduled return. Keep medium lethality and real interruption counterplay. Maintain persistent contender versus replaceable life identity. Reject stale-life and cross-owner requests.

Implement one real courier collect-and-deliver loop, a real Seal carry/drop/pickup lifecycle and the real ten second Heart channel, contest and single winning receipt. Acquisition of the demo Seal and setting defensive flags may be clearly labelled authority-only fixtures. They do not count as full crafting or tower-combat parity. Killing everyone must not award victory.

Implement server-enforced fog with explored terrain, explicit low/high policy, private ward, public sensor and source loss. Use the specified owner-only filtered relay and neutral local remote proxies unless a documented tested alternative is necessary. Do not replicate hidden canonical actors, private resources or human-to-body mappings through GameState, PlayerState, effects or default actor replication. A dark material alone is not fog enforcement.

Spawn eight nonviolent residents through authority with shared presentation. Keep the complete profession, crafting, tower and camp progression, autonomous contender strategies, full economy and population, room-code service and authenticated resume in the migration ledger as deferred or fixture coverage as specified. Do not portray the slice as full v0.2 parity.

## Scope discipline

Follow B30–B37 and keep runnable checkpoints. Do not add GAS, Lyra, online services, accounts, public matchmaking, squads, a character roster, marketplace dependencies or production deployment. Do not replace a missing engine test with a fabricated success. Resolve routine reversible implementation choices autonomously and document material deviations.

Use the installed toolchain for the required editor, packaged Windows and independent-remote-client tests. A packaged dedicated server is conditional on compatible engine build support. State explicitly when a source build is missing; an unbuilt Server target does not establish dedicated readiness. Listen-host testing does not protect information from the host who owns authority.

## Verify and deliver

Run the U01–U40 acceptance cases appropriate to each stage, pure helper tests, functional map tests, real network tests and the packaged demo. Test hidden-data paths, message reordering, concealment, source union, simultaneous damage, courier and item replay, possession changes and rematch cleanup. Verify UI and geometry in rendered play, not only through compilation.

Deliver source, essential assets, scripts, engine lock, setup, asset manifest, baseline audit, completed migration ledger, actual test logs, packaged Development build if successfully built, known limits and the short demo guide. Show actual results for cold build, cooking, two-client operation and network delay. Label each feature Ported, Replaced, Fixture only, Deferred or Deviated, and each test Passed, Failed, Not run or Conditional not available.

If an external dependency blocks a build, complete feasible source and configuration work, record the exact blocker and continuation steps, and state that acceptance remains incomplete. Do not stop at asking for permission for work already authorized. Keep synced project reference files under `sources/` unchanged.
