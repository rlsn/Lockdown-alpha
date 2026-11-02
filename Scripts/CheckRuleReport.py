"""Validate the complete set of currently supported native rule tests."""

import argparse
import json
from pathlib import Path


REQUIRED_TESTS = frozenset(
    {
        "Lockdown.Rules.CampDecision",
        "Lockdown.Rules.ControlMappings",
        "Lockdown.Rules.DemoContent.AtomicAssetRoundTrip",
        "Lockdown.Rules.DemoContent.CaseFoldedLookup",
        "Lockdown.Rules.DemoContent.DisplayStringTable",
        "Lockdown.Rules.DemoContent.StrictRefusal",
        "Lockdown.Rules.DemoContent.TypedProjection",
        "Lockdown.Rules.DemoCamp.Roster",
        "Lockdown.Rules.DemoCamp.Lifecycle",
        "Lockdown.Rules.DemoCamp.Retaliation",
        "Lockdown.Rules.DemoCamp.ReturnReset",
        "Lockdown.Rules.DemoCamp.ObservedFacts",
        "Lockdown.Rules.DemoCapture.Continuity",
        "Lockdown.Rules.DemoCapture.ProtectionAndContracts",
        "Lockdown.Rules.DemoCapture.Geometry",
        "Lockdown.Rules.DemoCapture.PublicViews",
        "Lockdown.Rules.DemoCapture.ProviderGate",
        "Lockdown.Rules.DemoCapture.SessionEndgame",
        "Lockdown.Rules.DemoCore.BodyIdentity",
        "Lockdown.Rules.DemoCore.CheckedTime",
        "Lockdown.Rules.DemoCore.ClockProjection",
        "Lockdown.Rules.DemoCore.ScopedAuthority",
        "Lockdown.Rules.DemoMotionProbe.Slide",
        "Lockdown.Rules.DemoMotionProbe.StepUpRollback",
        "Lockdown.Rules.DemoMotionProbe.LedgeRevert",
        "Lockdown.Rules.DemoMotionProbe.SupportTransitions",
        "Lockdown.Rules.DemoMotionProbe.UnrecordedMoveRejected",
        "Lockdown.Rules.DemoSpawn.UnbiasedSample",
        "Lockdown.Rules.DemoAttributeProjection.ApprovedLevels",
        "Lockdown.Rules.DemoAttributeProjection.DuplicateEquipment",
        "Lockdown.Rules.DemoAttributeProjection.ChoicesAndPassives",
        "Lockdown.Rules.DemoAttributeProjection.Envelope",
        "Lockdown.Rules.DemoAttributeProjection.Refusals",
        "Lockdown.Rules.DemoAttributeProjection.ResourceMaxChange",
        "Lockdown.Rules.DemoCombatEvaluation.Damage",
        "Lockdown.Rules.DemoCombatEvaluation.DamageDomains",
        "Lockdown.Rules.DemoCombatEvaluation.ExactDamage",
        "Lockdown.Rules.DemoCombatEvaluation.BasicTiming",
        "Lockdown.Rules.DemoCombatEvaluation.TimingDomains",
        "Lockdown.Rules.DemoCombatStatus.Cooldown",
        "Lockdown.Rules.DemoCombatStatus.Regen",
        "Lockdown.Rules.DemoCombatStatus.Roots",
        "Lockdown.Rules.DemoCombatStatus.Shield",
        "Lockdown.Rules.DemoCombatSources.CooldownTracks",
        "Lockdown.Rules.DemoEntryContract",
        "Lockdown.Rules.DemoEntryObservation",
        "Lockdown.Rules.DemoGrowthWire",
        "Lockdown.Rules.DemoGrowthLedger",
        "Lockdown.Rules.DemoGrowthRequests",
        "Lockdown.Rules.DemoGrowthProgression",
        "Lockdown.Rules.DemoGrowthPrivacySchema",
        "Lockdown.Rules.DemoInputByteIntegrity",
        "Lockdown.Rules.DemoLobbyAuthority",
        "Lockdown.Rules.DemoLobbyReadModel",
        "Lockdown.Rules.DemoMatchIdentity",
        "Lockdown.Rules.DemoOpponentPolicy.CampCost",
        "Lockdown.Rules.DemoOpponentPolicy.ChoiceOrder",
        "Lockdown.Rules.DemoOpponentPolicy.EquipmentPlan",
        "Lockdown.Rules.DemoOpponentPolicy.CaptureUrgency",
        "Lockdown.Rules.DemoOpponentPolicy.RetryBounds",
        "Lockdown.Rules.DemoOpponentPolicy.Priorities",
        "Lockdown.Rules.DemoOpponentPolicy.Projections",
        "Lockdown.Rules.DemoOpponentPolicy.Outcomes",
        "Lockdown.Rules.DemoOpponentPolicy.Requests",
        "Lockdown.Rules.DemoOrders.AttackMoveCandidate",
        "Lockdown.Rules.DemoOrders.BookIsolation",
        "Lockdown.Rules.DemoSessionReceipts",
        "Lockdown.Rules.DemoVision.Codec",
        "Lockdown.Rules.DemoVision.Privacy",
        "Lockdown.Rules.DemoVision.Ordering",
        "Lockdown.Rules.DemoVision.Handles",
        "Lockdown.Rules.DemoVision.Termination",
        "Lockdown.Rules.DemoVision.Sight",
        "Lockdown.Rules.DemoWorld.TypedProjection",
        "Lockdown.Rules.DemoWorld.ExactSignedGeometry",
        "Lockdown.Rules.DemoWorld.SavedCollisionResponses",
        "Lockdown.Rules.DemoWorld.SavedConvexTransform",
        "Lockdown.Rules.DemoWorld.AtomicAssetRoundTrip",
        "Lockdown.Rules.HUDLifeStatus",
        "Lockdown.Rules.HUDActionView",
        "Lockdown.Rules.HUDCombatOwnerView",
        "Lockdown.Rules.HUDContentParameters",
        "Lockdown.Rules.HUDOwnResources",
        "Lockdown.Rules.HUDPublicMatch",
        "Lockdown.Rules.HUDObservationBoundary",
        "Lockdown.Rules.IssuedCommandLiveness",
        "Lockdown.Rules.LocalControlRegistryIsolation",
        "Lockdown.Rules.LocalDisplayAutosaveGuard",
        "Lockdown.Rules.LocalDisplayNativeSaveFlush",
        "Lockdown.Rules.LocalDisplayPersistenceReadback",
        "Lockdown.Rules.LocalDisplayTrialLifecycle",
        "Lockdown.Rules.LocalSettingsPersistence",
        "Lockdown.Rules.LocalSettingsAtomicReplaceFailure",
        "Lockdown.Rules.LocalSettingsMetadataReadFailure",
        "Lockdown.Rules.LocalSettingsRejectionAndRecovery",
        "Lockdown.Rules.ViewPrivacySchema",
    }
)


def check_report(report):
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
    missing = REQUIRED_TESTS - seen
    unexpected = seen - REQUIRED_TESTS
    if missing or unexpected:
        raise ValueError(
            "Automation test set differs from the required registrations; "
            f"missing={sorted(missing)}, unexpected={sorted(unexpected)}"
        )
    return len(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    try:
        with args.report.open(encoding="utf-8-sig") as stream:
            count = check_report(json.load(stream))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Invalid rule report {args.report}: {error}\n")
    print(f"All {count} required native rule tests succeeded.")


if __name__ == "__main__":
    main()
