# B50 — v0.5 baseline and implementation readiness

21 October 2026. The revised master plan, all v0.5 specification chapters,
JSON contract, templates, frozen-reference manifest, three wireframes and B47
continuation were reviewed against the actual Unreal repository. This records
preparation and a fresh v0.4 smoke check; no v0.5 runtime feature is implemented.

## Baseline and evidence

- Handoff revision: `b8f003a6535079c98dd228cf45908eeff92f9411` on `main`.
- Runtime revision: `915f9e8e9b8fdc56febaf64703225dbea41d9dd5` (B47).
- Engine: Unreal 5.8.3 CL 58210709, installed distribution, retained through
  `Config/PlatformLock.json`. MSVC selected by the recorded builds is 14.44.35229;
  Windows SDK 10.0.26100.0 and .NET Framework SDK 4.8 are retained.
- Archive: `Artifacts/20261021-092725-233-PackageWin64/Archive/Windows`.
- Fresh provenance check: all 99 source/configuration hashes and seven archived
  payload hashes match `PackageManifest.json`; zero mismatches.
  Result: `Artifacts/V05ReadinessProvenance.json`.
- Fresh `pwsh -NoProfile -File Scripts/Preflight.ps1` passed engine/toolchain
  discovery. Evidence: `Artifacts/20261021-115739-161-Preflight/Environment.json`.
  This is discovery, not a new build.
- Fresh packaged regional smoke passed **93 checks**:
  `pwsh -NoProfile -File Scripts/RunDevelopmentCheck.ps1 -Region -PackageRoot
  'Artifacts/20261021-092725-233-PackageWin64/Archive/Windows'`.
  Evidence: `Artifacts/20261021-115804-620-Packaged-Region-DevelopmentCheck/Region.json`.
  It is the existing labelled automated probe, not human play or a new v0.5 pass.

The retained [B47 report](../v04/B47_IMPLEMENTATION.md) indexes Editor/Game builds,
28 generated packages, cook, delayed independent clients, privacy, ten resets and
the real ordinary 25-minute run. The [B47 continuation archive](../v04/CONTINUATION_B47.md)
preserves its full reproduction notes. No unrelated full suite or long soak was
repeated for this documentation change. Templates and predecessor snapshots remain
separate from the [working ledger](FeatureLedger.csv).

Documentation validation passed for 39 Markdown files and 111 local links,
U01–U56, 33 working-ledger rows, no same-context mapping conflicts and eight
byte-identical frozen predecessor references. Result:
`Artifacts/V05DocumentationValidation.json`.

## Actual delivered behavior and sprint disposition

| Area | Inspected v0.4 implementation | v0.5 disposition |
| --- | --- | --- |
| Profile | `V04`, 1–4 contenders, level five, four shared skills, 1500-second deadline | Retain gameplay and short integration boundary; update normal-profile UI/input deliberately |
| Framework | Enhanced Input, native UMG/Slate, native CharacterMovement, NavigationSystem/AIModule already linked | Extend existing architecture; no engine/GAS/CommonUI migration |
| Movement | WASD camera-relative input; saved sprint flag and correction counters; native bounded root-motion Step | Replace input with validated native destination orders; prove adapter on independent clients first |
| Facing | Cursor-ground polling rotates own body and sends aim updates | Replace with travel/explicit-action facing; hover/selection must not rotate the body |
| Keys | Generated WASD/LMB/Q/Space/E/F/H; native 1–4 skills and P development overlay | Replace conflicts coherently; Q/Space already require equipped skills in V04, so remove aliases rather than claiming an unslotted bypass exists |
| Camera | Yaw -45, pitch -60, arm 1100–1700 cm | Retain geometry/zoom; add bounded pan, follow/recenter and minimap commands |
| Holds | Owner refresh approximately 100 ms; objective, preparation and skill expiry 250 ms | Retain **250 ms**, replacing only the proposed 300 ms fallback in JSON |
| Privacy | Owner-only build/books/wallet; eight-field observed entity mask and current/explored grid | Reuse filtered views for inspector, hit testing, minimap and target snapshots; no arbitrary pawn reads |
| HUD/map | Existing native own hotbar, development panel and painted filtered minimap | Extend working data paths; replace presentation with shared widgets and input capture |
| Books | Unique `ALDBook` identities plus owner book list; four-book cap; 40-gold Guild purchase; three-second read | Unify 24-slot ownership, preserve learning/costs; add quote/versioned catalog UI |
| Starting items | Zero gold/XP, no books or Seal; **one finite ward charge** in `FLDOwnerState` | Convert exactly one starting charge into one item at clean-match initialization; no extra grants |
| Ward effect | 96 cm placement, 560 cm sight, 60 seconds, 20 HP, maximum two active sources | Retain all limits, including the active-source cap; consume one owned instance only after successful creation |
| Death | Books and Seal drop; unused ward counter explicitly becomes zero; return does not replenish it | Drop books/Seal; destroy unused carried charge instances once and clear their references; no respawn grant |
| Skills/progression | Six attributes, class/rank/subclass data, four equipped slots, skill-identity cooldowns, held station commits | Retain authority and tuning; inventory shortcuts never cast book skills |
| Economy/objective | Persistent theft wallets, bounded paid work/XP, Witness, once-only fabrication and earned Heart | Retain costs, receipts, RNG and transaction ordering; trade cannot escrow gold or reset crafting |
| Peer trade | No existing consensual item exchange/session model | New invitation, reservations, versioned terms, both Ready and separate final Confirms, atomic transfer |

Source anchors inspected: `LDPlayerController.cpp`, `LDPlayerDevelopment.cpp`,
`LDCharacter.cpp`, `LDCharacterMovementComponent.cpp`, `LDViewTypes.h`,
`LDVisionDirector.cpp`, `LDDevelopmentDirector.cpp`, `LDGameMode.cpp`,
`LDObjectiveRules.h`, `LDSkillExecution.cpp`, `LDUIRoot.cpp` and the bootstrap
input definitions. Existing B47 reports supply broader retained validation;
source inspection does not establish new remote navigation or UI behavior.

## Working implementation contract

Use the supplied D51–D63 defaults as implementation targets, with the recorded
baseline dispositions above. These are engineering defaults adopted from the
provided specification, not newly attributed direct user quotations or completed
gameplay. Preserve them unless actual implementation evidence requires a recorded
deviation synchronized across prose, JSON and assets.

- Right-click native navigation and observed-position approach; LMB inspection;
  A then click explicit melee; Q/W/E/R skills, 1–4 quick items and F held context.
- One controlled body, authorized selected inspector and anchored own commands;
  no mana, opponent sheet, hidden target tracking or remote interaction via towers.
- One 24-slot nonstacking carried bag, four ItemId references, one-Seal carry limit;
  retain actual supplies, learning, cooldowns and audited death policy.
- Guild books remain 40 gold. Peer trade allows six items per side and gold,
  with reservations, unshielded wallets, revised consent and final net limits.
- Explicit input priority, one major nonpausing panel, fresh input after close,
  rebinding, local preferences and the declared resolution/text-scale checks.
- Keep the city/two-town region, normal 25-minute profile and earned peaceful
  claim. Full conquest, expanded world, new abilities and 60–90 minute human pacing
  remain outside the sprint.

The charge-loss rule is an explicit disposition of D57, not an undecided fallback:
the delivered counter already has a death policy. Moving it into physical inventory
does not justify increasing recoverable supply. The exact finite count converts
once at a clean match boundary; carried charges transferred to another living
owner subsequently follow that owner's death transaction.

## Next implementation gate

Start **B51**, before full HUD expansion. The existing predicted CharacterMovement
and sprint saved moves make validated owner steering a candidate, not a proven
adapter. Compare the native integration options on the pinned engine and record
the one that passes independent owning-client tests. Do not select an untested
SimpleMove/AI possession replacement or create a second movement integrator.

1. Add match/life/owner-scoped order IDs, cancellation epochs, newest-order
   coalescing and separate navigation liveness; reuse native collision/corrections.
2. Validate native routes, intended-floor projection, complete paths, acceptance
   and snapshot approach. Preserve the cook-safe navigation envelope and humanoid
   nonblocking policy. Hidden dynamic positions must not enter command validation
   or previews; ground routes can use static geometry.
3. Prove ramps, walls/trees, unreachable floors, sprint, Step, replacement, Stop,
   focus loss, stale acknowledgements, death and respawn on two independent clients.
   Record measured RTT, movement corrections and stop/arrival behavior.
4. Then proceed B52 input ownership → B53 HUD/privacy → B54 items → B55 vendor →
   B56 peer exchange → B57 presentation → B58 delivery and user review.

Important integration risks are cursor-facing writes competing with navigation,
legacy generated contexts overlapping the new bindings, major panels leaking
world input, retained hold tokens surviving new targeting modes, and the current
separate book/ward/Seal stores being only partially migrated. Address these at
their dependency gates; do not mark downstream U checks passed from this review.

## Carried validation gaps

The interrupted technical soak is not a 90-minute pass. It remains deferred by
user direction; v0.5 does not require an automatic rerun absent a new lifetime
concern. B58 still requires affected lifetime checks, ten restarts and a normal
25-minute session. A fresh B47 cold checkout was not run, and this review does not
substitute for U49's future cold build/cook. Remote Git/LFS delivery and dedicated
packaging remain conditional/unverified. Authenticated resume is not retained.

No actual human multiplayer/usability, contrasting-build or long-session pacing
evidence was produced. U55 remains pending real participants; automated clients
and wireframes do not satisfy it. The v0.4 partial/deferred evidence remains
unchanged in its own ledger and acceptance table.
