"""Validate the selected native rule tests, or plan a run.

A full run must report exactly REQUIRED_TESTS. A run narrowed by path prefixes (--prefix) is a development aid and
never a gate. A run that only leaves out IMPORT_TESTS (--skip-import) is a gate only where CIChangeScope.py found no
change to their inputs. Runs without IMPORT_TESTS use the small runtime fixture profile instead of generating every
import mutation case.
"""

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITE = "Lockdown.Rules"


REQUIRED_TESTS = frozenset(
    {
        "Lockdown.Rules.DemoVision.GeometryParity",
        "Lockdown.Rules.DemoVision.CoveragePublication",
        "Lockdown.Rules.DemoVision.ArrivalRoutes",
        "Lockdown.Rules.DemoVision.PatrolCorridor",
        "Lockdown.Rules.ArtCatalog",
        "Lockdown.Rules.DemoGround.ParityAndCache",
        "Lockdown.Rules.DemoGround.BossBodyAdmission",
        "Lockdown.Rules.DemoMotionProbe.RampSeamSupport",
        "Lockdown.Rules.DemoMotionProbe.Stationary",
        "Lockdown.Rules.DemoMotionProbe.StationarySupport",
        "Lockdown.Rules.DemoGrowthProbe.CampDetourGeometry",
        "Lockdown.Rules.DemoGrowthProbe.HudRewardBinding",
        "Lockdown.Rules.ArtCatalogAsset",
        "Lockdown.Rules.ArtDecorations",
        "Lockdown.Rules.AudioCatalog",
        "Lockdown.Rules.AudioCatalogAsset",
        "Lockdown.Rules.AudioCueOwn",
        "Lockdown.Rules.AudioMusicSelection",
        "Lockdown.Rules.AudioCueBasicAttack",
        "Lockdown.Rules.AudioCuePublic",
        "Lockdown.Rules.BuildEvaluation.Horizon",
        "Lockdown.Rules.BuildEvaluation.Input",
        "Lockdown.Rules.BuildEvaluation.Policy",
        "Lockdown.Rules.BuildEvaluation.Seeds",
        "Lockdown.Rules.BuildEvaluation.WorkshopPlan",
        "Lockdown.Rules.BuildBlueprint.Serialize",
        "Lockdown.Rules.BuildBlueprint.Validate",
        "Lockdown.Rules.BuildBlueprint.Prune",
        "Lockdown.Rules.BuildBlueprint.Attribution",
        "Lockdown.Rules.BuildBlueprint.Components",
        "Lockdown.Rules.CampDecision",
        "Lockdown.Rules.CombatReceiptBurst",
        "Lockdown.Rules.ControlMappings",
        "Lockdown.Rules.DemoContent.AtomicAssetRoundTrip",
        "Lockdown.Rules.DemoContent.CaseFoldedLookup",
        "Lockdown.Rules.DemoContent.ComponentCatalog",
        "Lockdown.Rules.DemoContent.DevReload",
        "Lockdown.Rules.DemoContent.DisplayStringTable",
        "Lockdown.Rules.DemoContent.GrowthCatalog",
        "Lockdown.Rules.DemoContent.StrictRefusal",
        "Lockdown.Rules.DemoContent.TypedProjection",
        "Lockdown.Rules.DemoCamp.Roster",
        "Lockdown.Rules.DemoCamp.Lifecycle",
        "Lockdown.Rules.DemoCamp.Retaliation",
        "Lockdown.Rules.DemoCamp.ReturnReset",
        "Lockdown.Rules.DemoCamp.ReturnDeadEnd",
        "Lockdown.Rules.DemoCamp.Growth",
        "Lockdown.Rules.DemoAirdrop.Rules",
        "Lockdown.Rules.DemoAirdropApprovedSchedule",
        "Lockdown.Rules.DemoFieldMarkers",
        "Lockdown.Rules.DemoCharms.Rules",
        "Lockdown.Rules.DemoCharms.Draw",
        "Lockdown.Rules.DemoLeader.Rules",
        "Lockdown.Rules.DemoBoss.Rules",
        "Lockdown.Rules.DemoBossFieldLifecycle",
        "Lockdown.Rules.DemoCamp.ObservedFacts",
        "Lockdown.Rules.DemoCapture.SprayAndGuard",
        "Lockdown.Rules.DemoCapture.GuardFreeze",
        "Lockdown.Rules.DemoCapture.GuardNeedsOwner",
        "Lockdown.Rules.DemoCapture.SprayInterrupt",
        "Lockdown.Rules.DemoCapture.GraffitiCleared",
        "Lockdown.Rules.DemoCapture.CombatHitInterrupt",
        "Lockdown.Rules.DemoCapture.ProtectionAndContracts",
        "Lockdown.Rules.DemoCapture.Geometry",
        "Lockdown.Rules.DemoCapture.PublicViews",
        "Lockdown.Rules.DemoCapture.ProviderGate",
        "Lockdown.Rules.DemoCapture.SessionEndgame",
        "Lockdown.Rules.DemoCapture.ContestPause",
        "Lockdown.Rules.DemoCapture.PausedContestSteps",
        "Lockdown.Rules.DemoCombatPrimitives.HardControlDiminish",
        "Lockdown.Rules.DemoCombatPrimitives.ReduceReadyAt",
        "Lockdown.Rules.DemoCombatPrimitives.SlowCleanse",
        "Lockdown.Rules.DemoCombatPrimitives.CommitControl",
        "Lockdown.Rules.DemoSkillExpansion.DashCapsuleContact",
        "Lockdown.Rules.DemoSkillExpansion.MitigationAndAttackSlow",
        "Lockdown.Rules.DemoSkillExpansion.PublicEffects",
        "Lockdown.Rules.DemoSkillExpansion.CatalogGrowthBounds",
        "Lockdown.Rules.DemoBuildPrimitives.KeywordRules",
        "Lockdown.Rules.DemoBuildPrimitives.DamagePool",
        "Lockdown.Rules.DemoBuildPrimitives.Silence",
        "Lockdown.Rules.DemoBuildPrimitives.SharedBreachAndBurn",
        "Lockdown.Rules.DemoBuildPrimitives.PoolHits",
        "Lockdown.Rules.DemoBuildPrimitives.LifestealCap",
        "Lockdown.Rules.DemoBuildPrimitives.RootOnSlowed",
        "Lockdown.Rules.DemoBuildPrimitives.BuildSafeWindow",
        "Lockdown.Rules.DemoBuildPrimitives.ZonePulses",
        "Lockdown.Rules.DemoBuildPrimitives.EmpowerCharges",
        "Lockdown.Rules.DemoBuildPrimitives.FullDebuffObservation",
        "Lockdown.Rules.DemoElements.Rules",
        "Lockdown.Rules.DemoElements.FogAndSlot",
        "Lockdown.Rules.DemoElements.BraceAndGrievous",
        "Lockdown.Rules.Combat.Components.Projection",
        "Lockdown.Rules.Combat.Components.EmpowerMerge",
        "Lockdown.Rules.Combat.Components.DashEmpower",
        "Lockdown.Rules.Combat.Components.Ledger",
        "Lockdown.Rules.Combat.Components.Elements",
        "Lockdown.Rules.Combat.Workshop.Compile",
        "Lockdown.Rules.Combat.Workshop.ControlMark",
        "Lockdown.Rules.Combat.Workshop.KnockbackImpact",
        "Lockdown.Rules.Combat.Workshop.DamageDefer",
        "Lockdown.Rules.Combat.Workshop.Ledger",
        "Lockdown.Rules.Combat.Events.ControlSource",
        "Lockdown.Rules.Combat.Events.ComponentElementSource",
        "Lockdown.Rules.Combat.Events.ZoneElementBatch",
        "Lockdown.Rules.Combat.Events.LedgerSkipsSettles",
        "Lockdown.Rules.Combat.Events.TrapFuse",
        "Lockdown.Rules.Combat.Events.DebtBypassesBrace",
        "Lockdown.Rules.Combat.Events.ShieldBurstBudget",
        "Lockdown.Rules.Combat.Events.SpreadDamageOnly",
        "Lockdown.Rules.Combat.Events.ZoneHasteTeam",
        "Lockdown.Rules.Combat.Events.FireShieldWindows",
        "Lockdown.Rules.DemoCore.BodyIdentity",
        "Lockdown.Rules.DemoCore.CaptureCompletesInsideStep",
        "Lockdown.Rules.DemoCore.CheckedTime",
        "Lockdown.Rules.DemoCore.ClockProjection",
        "Lockdown.Rules.DemoCore.RewardSettlementBeforeClose",
        "Lockdown.Rules.DemoCore.ScopedAuthority",
        "Lockdown.Rules.DemoCoreProbe.ArrivalTurn",
        "Lockdown.Rules.DemoCoreProbe.GrowthTiming",
        "Lockdown.Rules.DemoMotionProbe.Slide",
        "Lockdown.Rules.DemoMotionProbe.StepUpRollback",
        "Lockdown.Rules.DemoMotionProbe.LedgeRevert",
        "Lockdown.Rules.DemoMotionProbe.SupportTransitions",
        "Lockdown.Rules.DemoMotionProbe.UnrecordedMoveRejected",
        "Lockdown.Rules.DemoMotionProbe.DashStopsAtWall",
        "Lockdown.Rules.DemoSpawn.UnbiasedSample",
        "Lockdown.Rules.DemoAttributeProjection.ApprovedLevels",
        "Lockdown.Rules.DemoAttributeProjection.StatPoints",
        "Lockdown.Rules.DemoAttributeProjection.Envelope",
        "Lockdown.Rules.DemoAttributeProjection.Refusals",
        "Lockdown.Rules.DemoAttributeProjection.ResourceMaxChange",
        "Lockdown.Rules.DemoAttributeProjection.TemporaryTerms",
        "Lockdown.Rules.DemoAttributeProjection.LowHPTerm",
        "Lockdown.Rules.DemoBuildExpansion.Slow",
        "Lockdown.Rules.DemoBuildExpansion.TimedBonus",
        "Lockdown.Rules.DemoBuildExpansion.ConditionalTriggers",
        "Lockdown.Rules.DemoCombatEvaluation.ControlAmplify",
        "Lockdown.Rules.DemoBodyMotion.Timelines",
        "Lockdown.Rules.DemoPathSteering.HeldGoal",
        "Lockdown.Rules.DemoPathSteering.RestCrowd",
        "Lockdown.Rules.DemoCombatCues.Selection",
        "Lockdown.Rules.DemoControlFeel.HeldMove",
        "Lockdown.Rules.DemoControlFeel.PreInput",
        "Lockdown.Rules.DemoControlFeel.ModelPicking",
        "Lockdown.Rules.DemoControlFeel.PresentedModelPicking",
        "Lockdown.Rules.LocalSettingsCameraZoom",
        "Lockdown.Rules.LocalSettingsZoomMigration",
        "Lockdown.Rules.DemoControlFeel.SourceBuffer",
        "Lockdown.Rules.DemoControlFeel.ShiftQueue",
        "Lockdown.Rules.DemoControlFeel.AttackClick",
        "Lockdown.Rules.DemoControlFeel.RefusedSkillKeepsOrder",
        "Lockdown.Rules.DemoControlFeel.AutoAttackAfterCast",
        "Lockdown.Rules.DemoControl.TauntForcedTarget",
        "Lockdown.Rules.DemoMechanics.ShieldEvents",
        "Lockdown.Rules.DemoMechanics.Knockback",
        "Lockdown.Rules.DemoMechanics.Periodic",
        "Lockdown.Rules.DemoControl.TauntRestore",
        "Lockdown.Rules.DemoCombatEvaluation.Damage",
        "Lockdown.Rules.DemoCombatEvaluation.DamageDomains",
        "Lockdown.Rules.DemoCombatEvaluation.ExactDamage",
        "Lockdown.Rules.DemoCombatEvaluation.BasicTiming",
        "Lockdown.Rules.DemoCombatEvaluation.TimingDomains",
        "Lockdown.Rules.DemoCombatStatus.Cooldown",
        "Lockdown.Rules.DemoCombatStatus.OutOfCombatRecovery",
        "Lockdown.Rules.DemoCombatStatus.DormRecovery",
        "Lockdown.Rules.DemoCombatStatus.Regen",
        "Lockdown.Rules.DemoCombatStatus.Roots",
        "Lockdown.Rules.DemoCombatStatus.Shield",
        "Lockdown.Rules.DemoCombatSources.CooldownTracks",
        "Lockdown.Rules.DemoCombatSources.InnateDash",
        "Lockdown.Rules.DemoCombatSources.CommittedBasicRelease",
        "Lockdown.Rules.DemoCombatSources.DirectSequenceWindow",
        "Lockdown.Rules.DemoControl.ActionMatrix",
        "Lockdown.Rules.DemoControl.EmpowerOnce",
        "Lockdown.Rules.DemoControl.ThornsRules",
        "Lockdown.Rules.DemoControl.ExpansionContent",
        "Lockdown.Rules.DemoEntryContract",
        "Lockdown.Rules.DemoEntryObservation",
        "Lockdown.Rules.DemoGrowthWire",
        "Lockdown.Rules.DemoGrowthRequests",
        "Lockdown.Rules.DemoGrowthProgression",
        "Lockdown.Rules.DemoGrowthOpening",
        "Lockdown.Rules.DemoGrowthOpeningPool",
        "Lockdown.Rules.DemoGrowthPrivacySchema",
        "Lockdown.Rules.DemoGrowthBuildRules",
        "Lockdown.Rules.DemoGrowthBuildCommands",
        "Lockdown.Rules.DemoAssist.Contributions",
        "Lockdown.Rules.DemoAssist.RewardSplit",
        "Lockdown.Rules.DemoAssist.KillFeedAndResult",
        "Lockdown.Rules.DemoAssist.Settlement",
        "Lockdown.Rules.DemoGrowthSkillGrowth",
        "Lockdown.Rules.DemoRest.Services.Gates",
        "Lockdown.Rules.DemoRest.Services.Npc",
        "Lockdown.Rules.DemoRest.Services.Respec",
        "Lockdown.Rules.DemoRest.Services.ReplaceForget",
        "Lockdown.Rules.DemoRest.Services.ForgetReclaim",
        "Lockdown.Rules.DemoFieldReward.Draw",
        "Lockdown.Rules.DemoFieldReward.Services",
        "Lockdown.Rules.DemoFieldReward.Choice",
        "Lockdown.Rules.DemoFieldReward.ReactionGap",
        "Lockdown.Rules.DemoRest.Timeline",
        "Lockdown.Rules.DemoRest.ReadySignup",
        "Lockdown.Rules.DemoRest.PhasePolicy",
        "Lockdown.Rules.DemoRest.EntryHold",
        "Lockdown.Rules.DemoRest.SpawnIdentity",
        "Lockdown.Rules.DemoPK.FormatDraw",
        "Lockdown.Rules.DemoPK.Outcome",
        "Lockdown.Rules.DemoPK.Ring",
        "Lockdown.Rules.DemoPK.Awards",
        "Lockdown.Rules.DemoPK.RingOutsideTime",
        "Lockdown.Rules.DemoGrowthComponents.InstanceDedupe",
        "Lockdown.Rules.DemoGrowthComponents.LevelGrantFixedSeed",
        "Lockdown.Rules.DemoGrowthComponents.InstallGate",
        "Lockdown.Rules.DemoGrowthComponents.OpeningBase",
        "Lockdown.Rules.DemoGrowthComponents.WorkshopTargets",
        "Lockdown.Rules.DemoDevTestCommands",
        "Lockdown.Rules.DemoInputByteIntegrity",
        "Lockdown.Rules.DemoLobbyAuthority",
        "Lockdown.Rules.DemoLobbyReadModel",
        "Lockdown.Rules.DemoMatchIdentity",
        "Lockdown.Rules.DemoOpponentPolicy.CampCost",
        "Lockdown.Rules.DemoOpponentPolicy.ChoiceOrder",
        "Lockdown.Rules.DemoOpponentPolicy.CaptureUrgency",
        "Lockdown.Rules.DemoOpponentPolicy.RetryBounds",
        "Lockdown.Rules.DemoOpponentPolicy.Priorities",
        "Lockdown.Rules.DemoOpponentPolicy.Projections",
        "Lockdown.Rules.DemoOpponentPolicy.Outcomes",
        "Lockdown.Rules.DemoOpponentPolicy.Requests",
        "Lockdown.Rules.DemoOpponentPolicy.InnateDash",
        "Lockdown.Rules.DemoOpponentPolicy.V7Activations",
        "Lockdown.Rules.DemoOpponentPolicy.RecoveryEscapeKept",
        "Lockdown.Rules.DemoOpponentPolicy.DormRecovery",
        "Lockdown.Rules.DemoOpponentPolicy.Airdrop",
        "Lockdown.Rules.DemoOpponentPolicy.Charms",
        "Lockdown.Rules.DemoOpponentPolicy.RecoveryCharmEscape",
        "Lockdown.Rules.DemoOpponentPolicy.HuntLeader",
        "Lockdown.Rules.DemoOpponentPolicy.AvoidBoss",
        "Lockdown.Rules.DemoOpponentPolicy.BossEscapeKept",
        "Lockdown.Rules.DemoOpponentPolicy.WalkRefusals",
        "Lockdown.Rules.DemoOpponentPolicy.Components",
        "Lockdown.Rules.DemoOpponentPolicy.PK",
        "Lockdown.Rules.DemoOpponentPolicy.WorkshopTargets",
        "Lockdown.Rules.DemoOpponentPolicy.ComponentChoice",
        "Lockdown.Rules.DemoOpponentPolicy.RestRebuild",
        "Lockdown.Rules.DemoOpponentPolicy.NpcGate",
        "Lockdown.Rules.DemoOpponentPolicy.RestServiceTravel",
        "Lockdown.Rules.DemoOpponentPolicy.Training",
        "Lockdown.Rules.DemoOpponentProbe.ReturnHold",
        "Lockdown.Rules.DemoOrders.AttackMoveCandidate",
        "Lockdown.Rules.DemoOrders.BookIsolation",
        "Lockdown.Rules.DemoOrders.ChasePress",
        "Lockdown.Rules.DemoOwnerViewTeardown",
        "Lockdown.Rules.DemoSessionReceipts",
        "Lockdown.Rules.DemoVision.Codec",
        "Lockdown.Rules.DemoVision.Privacy",
        "Lockdown.Rules.DemoVision.Ordering",
        "Lockdown.Rules.DemoVision.Handles",
        "Lockdown.Rules.DemoVision.Termination",
        "Lockdown.Rules.DemoVision.Sight",
        "Lockdown.Rules.DemoVision.TreeSight",
        "Lockdown.Rules.DemoVision.TieredSight",
        "Lockdown.Rules.DemoVision.ActionCues",
        "Lockdown.Rules.DemoVision.Projectiles",
        "Lockdown.Rules.DemoVision.EffectAccess",
        "Lockdown.Rules.DemoVision.Presentation",
        "Lockdown.Rules.DemoVision.PropFade",
        "Lockdown.Rules.DemoVision.FlightTrail",
        "Lockdown.Rules.DemoVision.OpponentActives",
        "Lockdown.Rules.DemoVision.FullStatusBound",
        "Lockdown.Rules.DemoWorld.TypedProjection",
        "Lockdown.Rules.DemoWorld.ExactSignedGeometry",
        "Lockdown.Rules.DemoWorld.SavedCollisionResponses",
        "Lockdown.Rules.DemoWorld.SavedConvexTransform",
        "Lockdown.Rules.DemoWorld.AtomicAssetRoundTrip",
        "Lockdown.Rules.HUDLifeStatus",
        "Lockdown.Rules.HUDActionView",
        "Lockdown.Rules.HUDCombatOwnerView",
        "Lockdown.Rules.HUDSelfIcons",
        "Lockdown.Rules.HUDIcons",
        "Lockdown.Rules.HUDContentParameters",
        "Lockdown.Rules.HUDCardText",
        "Lockdown.Rules.HUDCardFaces",
        "Lockdown.Rules.HUDBuildGate",
        "Lockdown.Rules.HUDWorkspacePrompt",
        "Lockdown.Rules.HUDCardSelfCheck",
        "Lockdown.Rules.HUDOwnResources",
        "Lockdown.Rules.HUDRegenDisplay",
        "Lockdown.Rules.DemoCombat.RegenRates",
        "Lockdown.Rules.HUDPublicMatch",
        "Lockdown.Rules.HUDMatchTimer",
        "Lockdown.Rules.HUDObservationBoundary",
        "Lockdown.Rules.HUDBasicAttackTerm",
        "Lockdown.Rules.HUDIdentityPalette",
        "Lockdown.Rules.HUDCooldownText",
        "Lockdown.Rules.HUDActionStates",
        "Lockdown.Rules.HUDDeathCause",
        "Lockdown.Rules.HUDKillFeedSelf",
        "Lockdown.Rules.HUDEventFeed",
        "Lockdown.Rules.HUDScoreboard",
        "Lockdown.Rules.LocalSettingsScoreboardMigration",
        "Lockdown.Rules.LocalSettingsDiagnostics",
        "Lockdown.Rules.LocalSettingsMarkMigration",
        "Lockdown.Rules.LocalSettingsItemKeysMigration",
        "Lockdown.Rules.LocalSettingsSkillEMigration",
        "Lockdown.Rules.PlaytestLog.Format",
        "Lockdown.Rules.PlaytestLog.RateLimit",
        "Lockdown.Rules.PlaytestLog.PeriodicGate",
        "Lockdown.Rules.PlaytestLog.PerfWindow",
        "Lockdown.Rules.PlaytestLog.MarkMatch",
        "Lockdown.Rules.PlaytestLog.HitVamp",
        "Lockdown.Rules.LocalSettingsPreferenceGroups",
        "Lockdown.Rules.DemoControlFeel.FixedCamera",
        "Lockdown.Rules.DemoControlFeel.OwnerCorrectionSmoothing",
        "Lockdown.Rules.HUDFrameDiagnostics",
        "Lockdown.Rules.HUDNextStep",
        "Lockdown.Rules.HUDRestTimeline",
        "Lockdown.Rules.HUDPKView",
        "Lockdown.Rules.HUDBuildGate",
        "Lockdown.Rules.HUDCardFaces",
        "Lockdown.Rules.HUDChoicesFold",
        "Lockdown.Rules.HUDComponents",
        "Lockdown.Rules.HUDWorkshopTargets",
        "Lockdown.Rules.HUDFieldRewardNotices",
        "Lockdown.Rules.HUDMaterialServices",
        "Lockdown.Rules.HUDDevTestPanel",
        "Lockdown.Rules.HUDTermsGlossary",
        "Lockdown.Rules.IssuedCommandLiveness",
        "Lockdown.Rules.LocalControlRegistryIsolation",
        "Lockdown.Rules.LocalDisplayBorderlessOnly",
        "Lockdown.Rules.LocalDisplayProductDefaults",
        "Lockdown.Rules.LocalDisplayNativeSaveFlush",
        "Lockdown.Rules.LocalDisplayPersistenceReadback",
        "Lockdown.Rules.LocalDisplayRenderScale",
        "Lockdown.Rules.LocalSettingsPersistence",
        "Lockdown.Rules.LocalSettingsAtomicReplaceFailure",
        "Lockdown.Rules.LocalSettingsCameraPanSpeed",
        "Lockdown.Rules.LocalSettingsMasterVolume",
        "Lockdown.Rules.LocalSettingsMusicVolume",
        "Lockdown.Rules.LocalSettingsMetadataReadFailure",
        "Lockdown.Rules.LocalSettingsRejectionAndRecovery",
        "Lockdown.Rules.LocalSettingsMinimapOrientation",
        "Lockdown.Rules.LocalSettingsQuickCast",
        "Lockdown.Rules.LocalSettingsAutoAttackAfterCast",
        "Lockdown.Rules.LocalSettingsDashMigration",
        "Lockdown.Rules.LocalSettingsTravelFlush",
        "Lockdown.Rules.Minimap.CameraUp",
        "Lockdown.Rules.Minimap.FacingUp",
        "Lockdown.Rules.Minimap.NotMirrored",
        "Lockdown.Rules.Minimap.NorthFixed",
        "Lockdown.Rules.Minimap.RoundTrip",
        "Lockdown.Rules.Minimap.FitsFrame",
        "Lockdown.Rules.Minimap.WholeGround",
        "Lockdown.Rules.Minimap.StaticInvalidation",
        "Lockdown.Rules.Minimap.CoverageTexture",
        "Lockdown.Rules.ViewPrivacySchema",
        "Lockdown.Rules.WorldFeedback.Priming",
        "Lockdown.Rules.WorldFeedback.OwnDamageAndHeal",
        "Lockdown.Rules.WorldFeedback.Shield",
        "Lockdown.Rules.WorldFeedback.VisibleUnits",
        "Lockdown.Rules.WorldFeedback.RewardsAndLevel",
        "Lockdown.Rules.WorldFeedback.Kill",
        "Lockdown.Rules.WorldFeedback.Text",
        "Lockdown.Rules.WorldFeedback.Casts",
        "Lockdown.Rules.WorldLabels.CampMemberName",
        "Lockdown.Rules.WorldLabels.ContenderName",
        "Lockdown.Rules.WorldLabels.StatusLine",
        "Lockdown.Rules.WorldLabels.Whitelist",
        "Lockdown.Rules.WorldLabels.HealthBar",
        "Lockdown.Rules.WorldLabels.OwnBars",
    }
)


# The data-import group: each test imports the exported fixture cases through the native importer and together they
# take most of a full run. CIChangeScope.py decides from the changed files whether a change can affect them.
IMPORT_TESTS = frozenset(
    {
        "Lockdown.Rules.DemoContent.AtomicAssetRoundTrip",
        "Lockdown.Rules.DemoContent.StrictRefusal",
        "Lockdown.Rules.DemoContent.TypedProjection",
        "Lockdown.Rules.DemoWorld.AtomicAssetRoundTrip",
        "Lockdown.Rules.DemoWorld.TypedProjection",
    }
)
assert IMPORT_TESTS <= REQUIRED_TESTS


def fixture_profile(selected):
    """Only the import tests need the complete valid/refusal fixture matrix."""
    return "full" if frozenset(selected) & IMPORT_TESTS else "runtime"

# UE keeps its whole command line in a fixed buffer (16384 characters); stay well inside it.
MAX_FILTER_CHARS = 8000
REGISTRATION = re.compile(r'IMPLEMENT_\w+_AUTOMATION_TEST\s*\(\s*\w+\s*,\s*"(Lockdown\.Rules\.[^"]+)"')


def registered_tests(root=ROOT):
    """Every Lockdown.Rules test registered in the native source, as written in its registration macro."""
    names = []
    for source in sorted((Path(root) / "Source").rglob("*.cpp")):
        names.extend(REGISTRATION.findall(source.read_text(encoding="utf-8-sig")))
    return names


def check_registrations(root=ROOT):
    """A narrowed run cannot see an unregistered or unlisted test, so the source must list exactly REQUIRED_TESTS."""
    names = registered_tests(root)
    duplicates = sorted({name for name in names if names.count(name) > 1})
    missing = REQUIRED_TESTS - set(names)
    unlisted = set(names) - REQUIRED_TESTS
    if duplicates or missing or unlisted:
        raise ValueError(
            "Native registrations differ from REQUIRED_TESTS; "
            f"duplicates={duplicates}, not registered={sorted(missing)}, not in REQUIRED_TESTS={sorted(unlisted)}"
        )


def select_tests(prefixes=(), skip_import=False):
    """The required tests a run must report: those under any of `prefixes` (all when none), less IMPORT_TESTS when
    `skip_import`."""
    for prefix in prefixes:
        if not isinstance(prefix, str) or not (prefix == SUITE or prefix.startswith(SUITE + ".")) \
                or re.search(r"[\s+^$,\"]", prefix):
            raise ValueError(f"Filter must be a plain {SUITE} test path prefix: {prefix!r}")
        if not any(name.startswith(prefix) for name in REQUIRED_TESTS):
            raise ValueError(f"Filter {prefix} matches no required test")
    selected = frozenset(
        name for name in REQUIRED_TESTS
        if (not prefixes or any(name.startswith(prefix) for prefix in prefixes))
        and not (skip_import and name in IMPORT_TESTS)
    )
    if not selected:
        raise ValueError("The filter leaves no test to run")
    return selected


def automation_filter(selected):
    """The `Automation RunTests` argument that runs exactly `selected`. UE matches each '+'-joined term against the full
    test path: '^' anchors it at the start and '$' at the end. A whole name group is one term when it leaves nothing
    out."""
    selected = frozenset(selected)
    if not selected <= REQUIRED_TESTS:
        raise ValueError(f"Not required tests: {sorted(selected - REQUIRED_TESTS)}")
    if selected == REQUIRED_TESTS:
        return SUITE
    left_out = REQUIRED_TESTS - selected
    terms = set()
    for name in selected:
        parts = name.split(".")
        term = f"^{name}$"
        for depth in range(3, len(parts)):
            group = ".".join(parts[:depth]) + "."
            if not any(other.startswith(group) for other in left_out):
                term = "^" + group
                break
        terms.add(term)
    result = "+".join(sorted(terms))
    if len(result) > MAX_FILTER_CHARS:
        raise ValueError(f"The test filter needs {len(result)} characters, more than the {MAX_FILTER_CHARS} allowed")
    return result


def check_report(report, expected=REQUIRED_TESTS):
    if not isinstance(report, dict):
        raise ValueError("Automation report must be an object")
    for field in ("failed", "notRun", "inProcess", "succeeded"):
        value = report.get(field)
        if isinstance(value, float) and value.is_integer():
            value = int(value)
        if type(value) is not int or value < 0:
            raise ValueError(f"Missing or invalid automation counter: {field}")
        if field != "succeeded" and value:
            raise ValueError(f"Automation has {field} tests: {value}")
    tests = report.get("tests")
    if not isinstance(tests, list):
        raise ValueError("Missing or invalid automation test results")
    seen = set()
    for test in tests:
        if not isinstance(test, dict):
            raise ValueError("Invalid automation test result")
        name = test.get("fullTestPath")
        if not isinstance(name, str) or not name:
            raise ValueError("Automation result has no fullTestPath")
        if name in seen:
            raise ValueError(f"Duplicate automation result: {name}")
        seen.add(name)
        if test.get("state") != "Success":
            raise ValueError(f"Automation test did not succeed: {name}")
    missing = expected - seen
    unexpected = seen - expected
    if missing or unexpected:
        raise ValueError(
            "Automation test set differs from the required registrations; "
            f"missing={sorted(missing)}, unexpected={sorted(unexpected)}"
        )
    return len(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report", type=Path, nargs="?", help="automation index.json to check")
    parser.add_argument("--prefix", action="append", default=[], help="only tests whose path starts with this; repeatable")
    parser.add_argument("--skip-import", action="store_true", help="leave out the data-import group")
    parser.add_argument("--plan", action="store_true", help="print the automation filter as JSON instead of checking")
    args = parser.parse_args()
    if bool(args.plan) == bool(args.report):
        parser.error("give either a report or --plan")
    try:
        expected = select_tests(args.prefix, args.skip_import)
        if expected != REQUIRED_TESTS:
            check_registrations()
        if args.plan:
            print(json.dumps({"filter": automation_filter(expected), "count": len(expected),
                              "required": len(REQUIRED_TESTS), "partial": bool(args.prefix),
                              "skipped_import": sorted(IMPORT_TESTS - expected) if args.skip_import else [],
                              "fixture_profile": fixture_profile(expected)}))
            return
        with args.report.open(encoding="utf-8-sig") as stream:
            count = check_report(json.load(stream), expected)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Invalid rule report {args.report or 'plan'}: {error}\n")
    if args.prefix:
        print(f"PARTIAL RUN, NOT A GATE: {count} of {len(REQUIRED_TESTS)} required native rule tests succeeded "
              f"(filter {' '.join(args.prefix)}).")
    elif args.skip_import:
        print(f"All {count} required native rule tests outside the data-import group succeeded; "
              f"the {len(IMPORT_TESTS)} data-import tests did not run.")
    else:
        print(f"All {count} required native rule tests succeeded.")


if __name__ == "__main__":
    main()
