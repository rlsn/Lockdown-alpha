# Platform and migration direction

## Chosen platform

The user selected Unreal and wants three dimensional or at least 2.5D presentation. v0.2 is reported complete and promising, but its graphics and UX obstruct serious evaluation. The new platform should make movement, depth, sight, threats and interactions easier to understand while retaining the gameplay hypotheses.

Unreal replaces the browser runtime for future development. The proposed first delivery is a native Windows desktop game with a controlled top down camera and grounded 3D movement. Low-friction access remains a product goal, but it no longer means that the client must run in a browser. No renewed engine-price comparison or licensing conclusion is part of this plan.

## What remains the game

Grand Heart acquisition remains the only ordinary victory. Peaceful and aggressive access routes, costly respawns, shared surveillance, private wards, terrain advantage, crowd concealment and medium lethality remain intended systems. Migrating platforms does not authorize returning to permanent elimination or survivor victory.

Three dimensional visuals do not implement those rules automatically. Server information filtering, source-specific vision, high-ground policy and readable warnings still need explicit design and tests. A camera that sees over a tree must not expose an actor the server has not authorized.

## The v0 3 milestone

Create a reproducible Unreal C++ project with real saved assets, useful native UI, a readable 3D test world, ordinary networked movement and combat, filtered observation, and representative courier, item and claim transactions. Preserve the old build for comparison.

This milestone is a scaffold with a working slice. Fixture-seeded Seals or diagnostic defensive states can test platform contracts without claiming full peaceful progression or assault parity. A completed migration ledger must identify the remaining systems. The next stage ports those systems before broader content or strategic conclusions.

## Engineering and operating boundaries

Prefer a small code-first core with deliberate Blueprint presentation. Pin the engine and toolchain, generate binary assets through Unreal, and verify a cooked Development package in addition to editor play. Prove restricted-client privacy against a separate server world rather than trusting a host-side dark overlay.

Local listen hosting can support the scaffold workflow. A packaged dedicated server depends on compatible engine build support and must be reported separately. Public operations, accounts, matchmaking, monetization and production deployment remain deferred. The user has chosen the engine; implementation should now focus on an honest, reviewable foundation.
