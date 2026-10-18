# Source history and platform decisions

## Source record

The new source is **Compare Web And Unity**, conversation `6ad41143-649c-83e8-995c-fba17457b6f4`. All four available user turns were read on 18 October 2026, with no older page reported. They establish:

1. v0.2 is complete according to the user; its gameplay seems fun and promising, while 2D graphics and UX obstruct serious testing.
2. The user wants 3D or at least 2.5D, particularly for the desired vision experience.
3. Unreal is the selected platform after the user's pricing comparison, and setup for Codex development is needed.
4. v0.3 should focus on migration from v0.2 and scaffolding the Unreal project.

Assistant replies were unreadable content-reference placeholders. No exact engine version, installation state, approved C++ architecture or licensing conclusion is inferred from them. The desire for 3D is a platform and presentation direction; this package does not claim that fog of war is technically impossible in 2D.

The existing master plan and v0.2 specification files were reviewed. No application repository or Unreal installation was supplied within this workspace. The source reports completion; independent code and toolchain verification is a future development task. Synced `sources/` remains read-only.

## Decision register

| ID | Decision | Basis and status |
| --- | --- | --- |
| P30 | Unreal is the next platform | User direction |
| P31 | Three dimensional world with controlled top down 2.5D gameplay | User direction for 3D; camera and movement interpretation proposed |
| P32 | Native Windows desktop first | Proposed scaffold boundary, replacing browser delivery |
| P33 | v0.3 is a scaffold plus working representative slice, not full parity | Proposed scope for the requested migration and scaffold release |
| P34 | Code-first C++ authority with focused Blueprint and UMG presentation | Proposed engineering choice |
| P35 | Pin an installed supported non-preview Unreal 5 patch after preflight | Proposed version policy; no particular patch assumed |
| P36 | Two centimetres per old pixel and 200 cm logical elevation steps | Proposed spatial conversion; timings and damage retained |
| P37 | Native Character Movement and explicit authority rule sampling | Proposed replacement for web movement and simulation integration |
| P38 | Owner-only filtered relay plus neutral local remote proxies | Proposed initial privacy design; no automatic default replication assumption |
| P39 | Local listen package required, separate remote-client privacy test required | Proposed validation boundary |
| P40 | Packaged dedicated server conditional on compatible source-build lane | Toolchain constraint informed by Epic documentation |
| P41 | Full crafting, tower progression, economy, AI goals and resume are inventoried rather than silently ported | Proposed scope control |
| P42 | The Grand Heart, peaceful and aggressive routes, costly death and concealment remain the product direction | Retained v0.2 intent |

The old v0.3 route and population iteration roadmap entry is superseded by the platform milestone. Those gameplay investigations move after the new foundation. Earlier web technology documents are retained as history and data sources, not native architecture requirements.

## Official technical references

Consulted 18 October 2026. The live documentation may display a newer engine branch than the developer's installed build. Use the selector and source matching the pinned patch; these links do not prescribe an upgrade.

| Reference | What it supports |
| --- | --- |
| [Visual Studio setup](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine) | Engine-specific compiler and SDK compatibility |
| [C++ quick start](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-cpp-quick-start?lang=en-US) | C++ project creation alongside Blueprint use |
| [Gameplay framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine) | Engine framework roles informing the project's responsibility split |
| [Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) | Actions, contexts and cancellation states |
| [Networked character movement](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine) | Native prediction, server correction and movement extension considerations |
| [Actor relevancy](https://dev.epicgames.com/documentation/unreal-engine/actor-relevancy-in-unreal-engine) | Per-connection relevancy and defaults that require deliberate auditing |
| [Actor ownership](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine) | Owning connections, replication and RPC routing |
| [Collision response](https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-response-reference-in-unreal-engine) | Distinct object, trace and response settings |
| [Automation framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine) | Native test infrastructure |
| [Packaging](https://dev.epicgames.com/documentation/unreal-engine/packaging-your-project) | Difference between build, cook, stage and package |
| [Dedicated server setup](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine) | The tutorial's source-engine and C++ prerequisites |

The observation relay, release scope, unit scale, fixture policy and test budgets are project design proposals. They are not Epic recommendations or measured results. No pricing or licence interpretation is included; the user's platform choice is accepted without making new financial or legal claims.
