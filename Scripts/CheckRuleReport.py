"""Validate the complete set of currently supported native rule tests."""

import argparse
import json
from pathlib import Path


REQUIRED_TESTS = frozenset(
    {
        "Lockdown.Rules.AcquisitionBoundedWire",
        "Lockdown.Rules.AcquisitionExactInputsAndWorkOrders",
        "Lockdown.Rules.AtomicWorkOrderLineageAndQualityReplay",
        "Lockdown.Rules.B90ApproachPaths",
        "Lockdown.Rules.B91FieldMask",
        "Lockdown.Rules.B91FieldWire",
        "Lockdown.Rules.B91Ignition",
        "Lockdown.Rules.B91NarrowBeamFootprint",
        "Lockdown.Rules.B92EconomyCatalog",
        "Lockdown.Rules.B92InheritedStarterAudit",
        "Lockdown.Rules.B92ServicePayload",
        "Lockdown.Rules.CargoCapacity",
        "Lockdown.Rules.CharacterGrowth",
        "Lockdown.Rules.CharacterPreparationPlan",
        "Lockdown.Rules.CharacterStarts",
        "Lockdown.Rules.CombatGeometry",
        "Lockdown.Rules.CompleteContentAndMajorCooldownFamilies",
        "Lockdown.Rules.ContentMultiTargetAndItemWire",
        "Lockdown.Rules.ControlMappings",
        "Lockdown.Rules.ControlProtectionAndBlind",
        "Lockdown.Rules.DeathCost",
        "Lockdown.Rules.DemoContent.AtomicAssetRoundTrip",
        "Lockdown.Rules.DemoContent.StrictRefusal",
        "Lockdown.Rules.DemoContent.TypedProjection",
        "Lockdown.Rules.DemoCore.BodyIdentity",
        "Lockdown.Rules.DemoCore.CheckedTime",
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
        "Lockdown.Rules.DemoEntryContract",
        "Lockdown.Rules.DemoEntryObservation",
        "Lockdown.Rules.DemoInputByteIntegrity",
        "Lockdown.Rules.DemoLobbyAuthority",
        "Lockdown.Rules.DemoLobbyReadModel",
        "Lockdown.Rules.DemoMatchIdentity",
        "Lockdown.Rules.DemoSessionReceipts",
        "Lockdown.Rules.DemoWorld.TypedProjection",
        "Lockdown.Rules.DemoWorld.ExactSignedGeometry",
        "Lockdown.Rules.DemoWorld.SavedCollisionResponses",
        "Lockdown.Rules.DemoWorld.SavedConvexTransform",
        "Lockdown.Rules.DemoWorld.AtomicAssetRoundTrip",
        "Lockdown.Rules.EconomyAccounting",
        "Lockdown.Rules.EconomyAtomicItemTransfer",
        "Lockdown.Rules.EconomyCatalog",
        "Lockdown.Rules.EconomyPayload",
        "Lockdown.Rules.EquipmentCapsAndCapacity",
        "Lockdown.Rules.GroundWireAndNaturalGates",
        "Lockdown.Rules.HUDObservationBoundary",
        "Lockdown.Rules.InventoryIdentity",
        "Lockdown.Rules.InventoryPrivacySchema",
        "Lockdown.Rules.IssuedCommandLiveness",
        "Lockdown.Rules.LoadoutWireAndQuickShape",
        "Lockdown.Rules.LocalControlRegistryIsolation",
        "Lockdown.Rules.LocalDisplayAutosaveGuard",
        "Lockdown.Rules.LocalDisplayNativeSaveFlush",
        "Lockdown.Rules.LocalDisplayPersistenceReadback",
        "Lockdown.Rules.LocalDisplayTrialLifecycle",
        "Lockdown.Rules.LocalSettingsPersistence",
        "Lockdown.Rules.LocalSettingsAtomicReplaceFailure",
        "Lockdown.Rules.LocalSettingsMetadataReadFailure",
        "Lockdown.Rules.LocalSettingsRejectionAndRecovery",
        "Lockdown.Rules.MapRiverContact",
        "Lockdown.Rules.NaturalGatesMasteryAndPotency",
        "Lockdown.Rules.ObjectiveBoundaries",
        "Lockdown.Rules.ObjectiveTiming",
        "Lockdown.Rules.PreparationWire",
        "Lockdown.Rules.QualityEligibilityCapsAndIdentity",
        "Lockdown.Rules.Region",
        "Lockdown.Rules.SkillCatalog",
        "Lockdown.Rules.SmokeUnionGeometry",
        "Lockdown.Rules.SpatialConversion",
        "Lockdown.Rules.TacticalResources",
        "Lockdown.Rules.TacticalStatus",
        "Lockdown.Rules.TacticalWireAndCatalog",
        "Lockdown.Rules.V08CompleteCatalogAndRecipes",
        "Lockdown.Rules.V08ObscuredUnionAndBoundedWire",
        "Lockdown.Rules.V08OwnerActionReadiness",
        "Lockdown.Rules.V08RiverFootprintsAndCatalog",
        "Lockdown.Rules.V08SelectiveTerrainUtilities",
        "Lockdown.Rules.V08SourcesTraitsAndWire",
        "Lockdown.Rules.V08TenEnvironmentalReactions",
        "Lockdown.Rules.V08WetFrostBoundary",
        "Lockdown.Rules.VendorPrivacySchema",
        "Lockdown.Rules.ViewOrdering",
        "Lockdown.Rules.ViewPayload",
        "Lockdown.Rules.ViewPrivacySchema",
        "Lockdown.Rules.WorldChecks",
        "Lockdown.Rules.WorldContractAndPublicCues",
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
