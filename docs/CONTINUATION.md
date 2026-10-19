# Continuation — B41 character development checkpoint

19 October 2026. B40 baseline verification and the B41 engineering foundation are
complete. The full v0.4 release is still in development. Read the
[implementation record](v04/IMPLEMENTATION_REPORT.md),
[working feature ledger](v04/FeatureLedger.csv),
[master revision 4](master/README.md) and [v0.4 package](v04/README.md).

## Current implementation

The saved `DA_CharacterCatalog` is generated from the versioned JSON companion.
`FLDCharacterBuild` lives in private contender owner state independently of the
pawn: class, six attributes, level, points, ranks, four slots, subclass, build
revision and absolute skill cooldown storage. Native movement derives speed from
DEX; basic melee snapshots STR damage. HP/damage retain fractional values.
Maximum-HP recalculation never heals. Death keeps the build, applies the new delay
and 15 percent loss, and returns a new life with full derived HP.

Frontend Host selects `?Profile=V04`. Class/Ready/start uses owning-controller RPCs.
The host starts 1–4 Ready contenders; one is practice. The 1500-second deadline
starts then. PreLogin and PostLogin both enforce admission; the engine session cap
is four. Gameplay is blocked in the lobby. Class choices and build state stay
private; only profile/start state and aggregate lobby counts are public.

The explicit legacy profile opens the sandbox without the option. Existing
rule/world/network regressions use it. Normal v0.4 hides/disables migration
fixtures and rejects global assassination/dash commands. Starting skills are
stored/equipped but cannot yet activate; the UI states this limitation.

## Next: B42

Implement one equipped-skill activation/arbitration path using the existing combat
sample, native movement and private observations. Add slots 1–4 and a mixed hotbar;
resolve skill from owned slot plus build revision. Basic melee remains a separate
action. Migrate Step/Ambush and implement the remaining catalog actions, durable
SkillId cooldowns, source snapshots and legal shared NPC entry points. Context,
old keys, direct RPCs and test wrappers must not bypass equipment.

`LDCharacterBuild` supplies pure preparation/rank/subclass/skill arithmetic for
subsequent work. It is not a held station transaction, book inventory, theft RNG
stream or complete active-skill implementation. B43–B46 still require station
commits, books, Charter checks, stable wallets/retries, world rewards, watchtower
sabotage and earned fabrication. Keep their acceptance rows open.

For B45, replace fixed site/district helpers and parameterize fog bounds, encoding
limits and minimap mapping together. Proposed towns/wilderness are outside the
current grid. Separate total paid-contract count from spendable commission credits;
the preview still uses the migration courier route. Full conquest remains deferred
and protected in normal v0.4.

## Runnable evidence and workflow

Latest package:
`Artifacts/20261019-095301-110-PackageWin64/Archive/Windows`.
The report indexes actual Editor/Game builds, 24 saved packages, 11 native rule
tests, 24 authority checks, two remote clients at configured 100/150 ms RTT with
jitter, ten v0.4 resets and retained migration regressions. Packaged local/network/
reset and 720p UI capture passed. These are engineering checks, not human matches
or complete release acceptance.

Use `RunDevelopmentCheck.ps1` with `-Network`, `-Reset`, `-CaptureUI -Height 720`
or `1080`, optionally with `-PackageRoot`. Packaged network tests use a labelled
headless listen authority and nonparticipating diagnostic local connection. They
do not certify a dedicated Server target or four-human capacity.

Keep Unreal 5.8.3 CL 58210709 and focused C++. Close the editor for native/reflected
changes; keep headers stable while UHT/builds run. Do not overlap builds/probes
with measured soaks. Bootstrap `Validate` writes no assets; `Regenerate` replaces
generated content explicitly. Preserve the separate web baseline and hand-authored
assets outside `/Game/Lockdown/Generated/`.

Human usability, dedicated packaging, authenticated resume, remote Git/LFS delivery
and full 60–90 minute pacing remain pending/conditional. The normal 25-minute run,
90-minute technical soak and six human matches belong to later B47 evidence.
