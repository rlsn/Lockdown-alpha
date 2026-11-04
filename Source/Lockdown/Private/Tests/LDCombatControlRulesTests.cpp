#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDAttributeProjection.h"
#include "UI/LDHUDModel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Misc/AutomationTest.h"
#include <limits>
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace LDCombatControlTestDetail
{
    // Same isolated approved_base fixture as the evaluator and source-rule tests; no live-data fallback.
    bool ReadApprovedContent(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
    {
        using namespace LDDemoContent;
        FString Root, Error;
        if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
        {
            Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first."));
            return false;
        }
        if (!Test.TestTrue(TEXT("Control-rule fixtures are isolated under this project's Artifacts"),
            FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
                FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
        {
            return false;
        }
        FCheck Check(Error);
        TArray<uint8> Bytes;
        FValue IndexValue;
        if (!ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
            || !Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
        {
            Test.AddError(Error);
            return false;
        }
        FFields Index(Check, IndexValue, TEXT("fixture index"));
        bool bTestOnly = false;
        const TArray<FValue>* Valid = nullptr;
        const TArray<FValue>* Invalid = nullptr;
        if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1"))
            || !Index.Flag(TEXT("test_only"), bTestOnly) || !Check.Require(bTestOnly, Index.Path, TEXT("test-only marker required"))
            || !Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
            || !Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
        {
            Test.AddError(Error);
            return false;
        }
        bool bFound = false;
        FLDDemoContentDigest Digest;
        for (const auto& RowValue : *Valid)
        {
            FFields Row(Check, RowValue, TEXT("fixture case"));
            FString Name;
            if (!Row.Identifier(TEXT("name"), Name))
            {
                Test.AddError(Error);
                return false;
            }
            if (Name != TEXT("approved_base")) { continue; }
            const TArray<FValue>* ChangedPaths = nullptr;
            FString MutatedQuantity;
            if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
                || !Row.Digest(TEXT("catalog_sha256"), Digest)
                || !Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, MaxFileBytes)
                || !Row.Text(TEXT("mutated_quantity"), MutatedQuantity, 0) || !Row.Done())
            {
                Test.AddError(Error);
                return false;
            }
            bFound = true;
        }
        if (!Test.TestTrue(TEXT("approved_base fixture is present"), bFound)) { return false; }
        FLDDemoContentBundle Bundle;
        auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
        TUniquePtr<FLDDemoPreparedContent> Prepared;
        if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
            || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
            || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
        {
            Test.AddError(Error);
            return false;
        }
        return true;
    }

    FLDDemoRational Rational(int64 Numerator, int64 Denominator = 1)
    {
        FLDDemoRational Value;
        Value.Numerator = Numerator;
        Value.Denominator = Denominator;
        return Value;
    }

    FLDCombatRoot Instance(int64 ExpiresAtMs, const FGuid& Source = FGuid::NewGuid())
    {
        FLDCombatRoot Control;
        Control.EffectId = FGuid::NewGuid();
        Control.SourceEntity = Source;
        Control.ExpiresAtMs = ExpiresAtMs;
        return Control;
    }

    FLDDemoPolicies RootPolicies()
    {
        FLDDemoPolicies Policies;
        Policies.RootBlocks = {ELDDemoRootBlock::Move, ELDDemoRootBlock::Dash};
        return Policies;
    }

    FLDDemoEffectDefinition Thorns(bool bAbility, bool bRearExempt)
    {
        FLDDemoEffectDefinition Effect;
        Effect.Opcode = uint8(ELDDemoEffectOp::Thorns);
        Effect.BaseP2 = 1500;
        Effect.ArmorRatioBp = 1500;
        Effect.RadiusCm = Rational(300);
        Effect.IntervalMs = 500;
        Effect.CriticalMultiplierBp = 20000;
        Effect.bAbilityTrigger = bAbility;
        Effect.bRearExempt = bRearExempt;
        Effect.DamageType = ELDDemoDamageType::Magical;
        return Effect;
    }

    const FLDDemoEffectDefinition* FindOp(const TArray<FLDDemoEffectDefinition>& Effects, ELDDemoEffectOp Op)
    { return Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; }); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatControlMatrixTest, "Lockdown.Rules.DemoControl.ActionMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatControlMatrixTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControlTestDetail;
    using EAction = ELDCombatControlAction;
    const FLDDemoPolicies Policies = RootPolicies();
    const TArray<FLDCombatRoot> None;
    const TArray<FLDCombatRoot> Active = {Instance(2000)};
    const EAction Actions[] = {EAction::Move, EAction::Displacement, EAction::Basic, EAction::Cast, EAction::Gear};
    // Expected matrix rows: Root, Disarm, Stun; columns follow Actions.
    const bool RootRow[] = {true, true, false, false, false};
    const bool DisarmRow[] = {false, false, true, false, false};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Actions); ++Index)
    {
        TestEqual(FString::Printf(TEXT("Root column %d"), Index), LDCombatControl::Blocks(Active, None, None, 1000, Policies, Actions[Index]), RootRow[Index]);
        TestEqual(FString::Printf(TEXT("Disarm column %d"), Index), LDCombatControl::Blocks(None, Active, None, 1000, Policies, Actions[Index]), DisarmRow[Index]);
        TestTrue(FString::Printf(TEXT("Stun blocks column %d"), Index), LDCombatControl::Blocks(None, None, Active, 1000, Policies, Actions[Index]));
        TestFalse(FString::Printf(TEXT("Nothing blocks column %d"), Index), LDCombatControl::Blocks(None, None, None, 1000, Policies, Actions[Index]));
        TestFalse(FString::Printf(TEXT("Expiry is half-open, column %d"), Index), LDCombatControl::Blocks(Active, Active, Active, 2000, Policies, Actions[Index]));
    }
    TestTrue(TEXT("Disarm displays only the basic block"), LDCombatControl::DisarmBlockBits() == uint8(ELDCombatControlBlock::Basic));
    const uint8 All = uint8(uint8(ELDCombatControlBlock::Move) | uint8(ELDCombatControlBlock::Basic) | uint8(ELDCombatControlBlock::Cast)
        | uint8(ELDCombatControlBlock::Gear) | uint8(ELDCombatControlBlock::Displacement));
    TestTrue(TEXT("Stun displays every block"), LDCombatControl::StunBlockBits() == All);

    TArray<FLDCombatRoot> Disarms, Stuns;
    const FLDCombatRoot Same = Instance(1500);
    TestTrue(TEXT("A new instance is added"), LDCombatControl::Apply(Stuns, Same, 1000));
    FLDCombatRoot Renewed = Same;
    Renewed.ExpiresAtMs = 9000;
    TestFalse(TEXT("The same effect never renews itself"), LDCombatControl::Apply(Stuns, Renewed, 1100));
    TestFalse(TEXT("An already expired instance is not added"), LDCombatControl::Apply(Disarms, Instance(1000), 1000));
    Disarms.Add(Instance(3000));
    TestFalse(TEXT("Nothing ends before its deadline"), LDCombatControl::ExpireDisarmsAndStuns(Disarms, Stuns, 1499));
    TestTrue(TEXT("The stun ends at its deadline"), LDCombatControl::ExpireDisarmsAndStuns(Disarms, Stuns, 1500) && Stuns.IsEmpty() && Disarms.Num() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatRearArcTest, "Lockdown.Rules.DemoControl.RearArc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatRearArcTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControlTestDetail;
    const FLDDemoRational Arc = Rational(120);
    const FVector Target(100, 200, 0);
    const auto At = [&Target](double Degrees) { return Target + FVector(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees)), 0) * 150.; };
    // Facing +X (yaw 0): directly behind is 180 degrees; the 120-degree sector spans 120..240.
    TestTrue(TEXT("Directly behind"), LDCombatControl::IsBehind(Target, 0, At(180), Arc));
    TestTrue(TEXT("Inside the sector edge"), LDCombatControl::IsBehind(Target, 0, At(125), Arc));
    TestTrue(TEXT("Exactly on the sector edge counts"), LDCombatControl::IsBehind(Target, 0, At(240), Arc));
    TestFalse(TEXT("Just outside the sector"), LDCombatControl::IsBehind(Target, 0, At(115), Arc));
    TestFalse(TEXT("In front is never behind"), LDCombatControl::IsBehind(Target, 0, At(0), Arc));
    TestFalse(TEXT("A side is not behind"), LDCombatControl::IsBehind(Target, 0, At(90), Arc));
    TestTrue(TEXT("The facing decides: yaw 90 puts the back at -90"), LDCombatControl::IsBehind(Target, 90, At(-90), Arc));
    TestFalse(TEXT("The same point is in front after turning"), LDCombatControl::IsBehind(Target, -90, At(-90), Arc));
    TestTrue(TEXT("Height does not matter"), LDCombatControl::IsBehind(Target, 0, At(180) + FVector(0, 0, 300), Arc));
    TestFalse(TEXT("Coincident feet are never behind"), LDCombatControl::IsBehind(Target, 0, Target, Arc));
    TestFalse(TEXT("An empty arc never matches"), LDCombatControl::IsBehind(Target, 0, At(180), Rational(0)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEmpowerTest, "Lockdown.Rules.DemoControl.EmpowerOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEmpowerTest::RunTest(const FString& Parameters)
{
    TOptional<FLDCombatEmpower> Armed;
    TestFalse(TEXT("Nothing armed, nothing taken"), LDCombatControl::ConsumeEmpower(Armed, 100).IsSet());
    FLDCombatEmpower Value;
    Value.EffectId = FGuid::NewGuid();
    Value.BaseP2 = 5000;
    Value.ADRatioBp = 4000;
    Value.ExpiresAtMs = 3100;
    Armed = Value;
    const TOptional<FLDCombatEmpower> First = LDCombatControl::ConsumeEmpower(Armed, 3099);
    TestTrue(TEXT("The first landed basic takes the armed bonus"), First.IsSet() && First->EffectId == Value.EffectId && First->BaseP2 == 5000);
    TestFalse(TEXT("It is consumed exactly once"), Armed.IsSet() || LDCombatControl::ConsumeEmpower(Armed, 3099).IsSet());
    Armed = Value;
    TestFalse(TEXT("At its deadline it has expired and gives nothing"), LDCombatControl::ConsumeEmpower(Armed, 3100).IsSet());
    TestFalse(TEXT("An expired bonus is dropped, not kept for later"), Armed.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatThornsTest, "Lockdown.Rules.DemoControl.ThornsRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatThornsTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControlTestDetail;
    const FLDDemoEffectDefinition Both = Thorns(true, true);
    const FLDDemoEffectDefinition BasicOnly = Thorns(false, false);
    const FGuid Attacker = FGuid::NewGuid(), Other = FGuid::NewGuid();
    const FString Key(TEXT("passive_iron_crotch"));
    TArray<FLDThornsMark> Marks;
    FLDThornsContact Basic;
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageP2 = 5000;
    Basic.DistanceCm = 200;
    TestTrue(TEXT("A primary basic hit in reach triggers"), LDCombatControl::ThornsTriggers(Both, Basic, Marks, Key, Attacker, 1000));
    FLDThornsContact Ability = Basic;
    Ability.HitClass = ELDCombatHitClass::Ability;
    TestTrue(TEXT("An ability hit triggers the basic-or-ability form"), LDCombatControl::ThornsTriggers(Both, Ability, Marks, Key, Attacker, 1000));
    TestFalse(TEXT("An ability hit never triggers the basic-only form"), LDCombatControl::ThornsTriggers(BasicOnly, Ability, Marks, Key, Attacker, 1000));
    FLDThornsContact Secondary = Basic;
    Secondary.HitClass = ELDCombatHitClass::BasicSecondary;
    TestFalse(TEXT("A cleave secondary never triggers"), LDCombatControl::ThornsTriggers(Both, Secondary, Marks, Key, Attacker, 1000));
    FLDThornsContact Reflected = Ability;
    Reflected.bReflected = true;
    TestFalse(TEXT("A reflection never triggers another reflection"), LDCombatControl::ThornsTriggers(Both, Reflected, Marks, Key, Attacker, 1000));
    FLDThornsContact Far = Basic;
    Far.DistanceCm = 300.5;
    TestFalse(TEXT("Beyond reach does not trigger"), LDCombatControl::ThornsTriggers(Both, Far, Marks, Key, Attacker, 1000));
    Far.DistanceCm = 300;
    TestTrue(TEXT("Exactly at reach triggers"), LDCombatControl::ThornsTriggers(Both, Far, Marks, Key, Attacker, 1000));
    FLDThornsContact Rear = Basic;
    Rear.bFromBehind = true;
    TestFalse(TEXT("A rear-exempt form ignores hits from behind"), LDCombatControl::ThornsTriggers(Both, Rear, Marks, Key, Attacker, 1000));
    TestTrue(TEXT("A form without the exemption answers hits from behind"), LDCombatControl::ThornsTriggers(BasicOnly, Rear, Marks, Key, Attacker, 1000));
    FLDThornsContact Zero = Basic;
    Zero.DamageP2 = 0;
    TestFalse(TEXT("A hit that dealt nothing does not trigger"), LDCombatControl::ThornsTriggers(Both, Zero, Marks, Key, Attacker, 1000));

    LDCombatControl::MarkThorns(Marks, Key, Attacker, 1000, Both.IntervalMs);
    TestFalse(TEXT("The same attacker is answered at most once per window"), LDCombatControl::ThornsTriggers(Both, Basic, Marks, Key, Attacker, 1499));
    TestTrue(TEXT("Another attacker has its own window"), LDCombatControl::ThornsTriggers(Both, Basic, Marks, Key, Other, 1499));
    TestTrue(TEXT("Another source has its own window"), LDCombatControl::ThornsTriggers(BasicOnly, Basic, Marks, TEXT("passive_prickle"), Attacker, 1499));
    TestTrue(TEXT("The window is half-open"), LDCombatControl::ThornsTriggers(Both, Basic, Marks, Key, Attacker, 1500));
    LDCombatControl::MarkThorns(Marks, Key, Other, 1700, Both.IntervalMs);
    TestTrue(TEXT("Expired marks are forgotten"), Marks.Num() == 1 && Marks[0].Attacker == Other);

    int64 Amount = 0;
    FString Error;
    TestTrue(TEXT("Base plus armor scaling"), LDCombatControl::ThornsBaseP2(Both, 4000, false, Amount, Error) && Amount == 1500 + 4000 * 1500 / 10000);
    TestTrue(TEXT("A critical hit applies the critical multiplier"), LDCombatControl::ThornsBaseP2(Both, 4000, true, Amount, Error) && Amount == (1500 + 600) * 2);
    TestTrue(TEXT("A multiplier of one leaves a critical reflection unchanged"),
        LDCombatControl::ThornsBaseP2(Thorns(false, false), 0, true, Amount, Error) && Amount == 1500 * 2);
    FLDDemoEffectDefinition Plain = BasicOnly;
    Plain.CriticalMultiplierBp = 10000;
    TestTrue(TEXT("No critical doubling without a multiplier"), LDCombatControl::ThornsBaseP2(Plain, 0, true, Amount, Error) && Amount == 1500);
    TestFalse(TEXT("Negative armor is refused"), LDCombatControl::ThornsBaseP2(Both, -1, false, Amount, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatJumpPlanTest, "Lockdown.Rules.DemoControl.JumpPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatJumpPlanTest::RunTest(const FString& Parameters)
{
    FVector Direction;
    double Distance = 0;
    const FVector Foot(10, 20, 30);
    TestTrue(TEXT("A point within reach is reached exactly"),
        LDCombatControl::PlanJump(Foot, Foot + FVector(300, 0, 55), 400, Direction, Distance)
        && FMath::IsNearlyEqual(Distance, 300.) && Direction.Equals(FVector(1, 0, 0)));
    TestTrue(TEXT("A farther point is clamped to the reach along its direction"),
        LDCombatControl::PlanJump(Foot, Foot + FVector(0, -900, 0), 400, Direction, Distance)
        && FMath::IsNearlyEqual(Distance, 400.) && Direction.Equals(FVector(0, -1, 0)));
    TestTrue(TEXT("The foot itself is a jump in place"),
        LDCombatControl::PlanJump(Foot, Foot + FVector(0.2, 0.2, 80), 400, Direction, Distance) && Distance == 0 && Direction.IsZero());
    TestFalse(TEXT("A non-finite point is refused"),
        LDCombatControl::PlanJump(Foot, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 400, Direction, Distance));
    TestFalse(TEXT("A jump without reach is refused"), LDCombatControl::PlanJump(Foot, Foot, 0, Direction, Distance));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatExpansionContentTest, "Lockdown.Rules.DemoControl.ExpansionContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatExpansionContentTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControlTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!ReadApprovedContent(*this, Content)) { return false; }
    FString Error;
    const FLDDemoAbilityDefinition* Peach = nullptr;
    const FLDDemoAbilityDefinition* Poke = nullptr;
    const FLDDemoAbilityDefinition* Lunge = nullptr;
    const FLDDemoAbilityDefinition* Crotch = nullptr;
    const FLDDemoEquipmentDefinition* Uniform = nullptr;
    if (!Content->FindAbility(TEXT("active_monkey_peach"), Peach, Error) || !Content->FindAbility(TEXT("active_thousand_year"), Poke, Error)
        || !Content->FindAbility(TEXT("active_lunge"), Lunge, Error) || !Content->FindAbility(TEXT("passive_iron_crotch"), Crotch, Error)
        || !Content->FindEquipment(TEXT("item_nail_uniform"), Uniform, Error))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Rear arc is a typed proper sector"), Content->Get().Parameters.RearArcDeg.Numerator > 0
        && Content->Get().Parameters.RearArcDeg.Numerator < 360 * Content->Get().Parameters.RearArcDeg.Denominator);
    for (const FLDDemoRankDefinition& Rank : Peach->Ranks)
    {
        const FLDDemoEffectDefinition* Disarm = FindOp(Rank.Effects, ELDDemoEffectOp::Disarm);
        TestTrue(TEXT("Every peach rank damages and disarms a unit"), Peach->Target.Mode == ELDDemoTargetMode::Unit
            && FindOp(Rank.Effects, ELDDemoEffectOp::Damage) && Disarm && Disarm->DurationMs > 0);
    }
    for (const FLDDemoRankDefinition& Rank : Poke->Ranks)
    {
        const FLDDemoEffectDefinition* Rear = FindOp(Rank.Effects, ELDDemoEffectOp::Backstab);
        TestTrue(TEXT("Every rear-strike rank amplifies its damage and stuns"), FindOp(Rank.Effects, ELDDemoEffectOp::Damage)
            && Rear && Rear->RatioBp > 0 && Rear->DurationMs > 0);
    }
    for (const FLDDemoRankDefinition& Rank : Lunge->Ranks)
    {
        const FLDDemoEffectDefinition* Empower = FindOp(Rank.Effects, ELDDemoEffectOp::EmpowerBasic);
        TestTrue(TEXT("Every lunge rank dashes and arms one empowered basic"), Lunge->Target.Mode == ELDDemoTargetMode::DirectionDash
            && FindOp(Rank.Effects, ELDDemoEffectOp::Dash) && Empower && Empower->DurationMs > 0
            && (Empower->BaseP2 > 0 || Empower->ADRatioBp > 0));
    }
    const FLDDemoEffectDefinition* CrotchThorns = FindOp(Crotch->Ranks[1].Effects, ELDDemoEffectOp::Thorns);
    const FLDDemoEffectDefinition* UniformThorns = Uniform->HasPassive ? FindOp(Uniform->Passive.Effects, ELDDemoEffectOp::Thorns) : nullptr;
    if (!TestTrue(TEXT("Both reflections import as typed thorns"), CrotchThorns && UniformThorns)) { return false; }
    TestTrue(TEXT("Iron crotch answers basics and abilities but not hits from behind"), CrotchThorns->bAbilityTrigger && CrotchThorns->bRearExempt);
    TestTrue(TEXT("The uniform answers basics only and from any side"), !UniformThorns->bAbilityTrigger && !UniformThorns->bRearExempt);

    // A learned trigger passive projects as a rank-exact combat trigger, never an attribute term.
    const FLDAttributeBuildValues Build(3, {FLDLearnedPassiveValue(TEXT("passive_iron_crotch"), 2)}, {}, {TEXT("item_nail_uniform")});
    const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(Build, *Content);
    if (!TestTrue(TEXT("The thorns build projects"), Projected.IsSuccess()))
    {
        AddError(Projected.Error.Field + TEXT(": ") + Projected.Error.Message);
        return false;
    }
    const FLDAttributeProjection& Projection = Projected.Value.GetValue();
    TestTrue(TEXT("The learned trigger keeps its rank"), Projection.LearnedTriggers.Num() == 1
        && Projection.LearnedTriggers[0].AbilityId == TEXT("passive_iron_crotch") && Projection.LearnedTriggers[0].Rank == 2);
    TestTrue(TEXT("The uniform's reflection is one equipment family"), Projection.EquipmentPassives.ContainsByPredicate(
        [](const FLDEquipmentPassiveFamily& Family) { return Family.Family == TEXT("passive_prickle"); }));

    // The rear strike multiplies the raw amount before resistance with the catalog's own formula.
    const FLDDemoEffectDefinition* Damage = FindOp(Poke->Ranks[0].Effects, ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* Rear = FindOp(Poke->Ranks[0].Effects, ELDDemoEffectOp::Backstab);
    FLDDamageEvaluationInput Input;
    Input.Definition.Emplace(Damage->BaseP2, Damage->ADRatioBp, Damage->APRatioBp, Damage->DamageType);
    Input.HitClass = ELDCombatHitClass::Ability;
    Input.bCritical = false;
    Input.SourceADP2 = Content->Get().Parameters.AD.BaseP2;
    Input.SourceAPP2 = Content->Get().Parameters.AP.BaseP2;
    Input.TargetResistanceP2 = 0;
    Input.TargetHPP2 = MAX_int32;
    Input.TargetShieldP2 = 0;
    Input.bTargetProtected = false;
    const FLDDamageEvaluationResult Front = LDTactical::EvaluateDamage(Input, *Content);
    Input.AmplifyBp = 10000 + Rear->RatioBp;
    const FLDDamageEvaluationResult Behind = LDTactical::EvaluateDamage(Input, *Content);
    if (!TestTrue(TEXT("Both strikes evaluate"), Front.IsSuccess() && Behind.IsSuccess())) { return false; }
    const int64 Raw = Damage->BaseP2 + Input.SourceADP2.GetValue() * Damage->ADRatioBp / 10000 + Input.SourceAPP2.GetValue() * Damage->APRatioBp / 10000;
    TestTrue(TEXT("Front strike is the plain amount"), FMath::Abs(Front.Value->DamageP2 - Raw) <= 1);
    TestTrue(TEXT("Rear strike is amplified by the authored ratio"),
        FMath::Abs(Behind.Value->DamageP2 - Raw * (10000 + Rear->RatioBp) / 10000) <= 1);
    Input.AmplifyBp = -1;
    TestFalse(TEXT("A negative amplification is refused"), LDTactical::EvaluateDamage(Input, *Content).IsSuccess());

    // Functional display: every new effect, target and status has explicit player text.
    for (const FLDDemoAbilityDefinition* Ability : {Peach, Poke, Lunge, Crotch})
    {
        for (const FLDDemoEffectDefinition& Effect : Ability->Ranks[0].Effects)
        {
            TestFalse(FString::Printf(TEXT("%s effect %u has a display line"), *Ability->Id, Effect.Opcode),
                LDHUD::EffectLine(Effect).StartsWith(TEXT("未知")));
        }
    }
    TestFalse(TEXT("The uniform reflection has a display line"), LDHUD::EffectLine(*UniformThorns).StartsWith(TEXT("未知")));
    FLDDemoTargetDefinition Point;
    Point.Mode = ELDDemoTargetMode::GroundPoint;
    Point.RangeCm = Rational(400);
    TestFalse(TEXT("A ground point target has a display line"), LDHUD::TargetLine(Point).StartsWith(TEXT("未知")));
    FLDDemoEffectDefinition Jump;
    Jump.Opcode = uint8(ELDDemoEffectOp::Jump);
    Jump.DurationMs = 400;
    TestFalse(TEXT("A jump has a display line"), LDHUD::EffectLine(Jump).StartsWith(TEXT("未知")));
    for (const ELDDemoStatusId Status : {ELDDemoStatusId::Disarm, ELDDemoStatusId::Stun, ELDDemoStatusId::Empowered})
    {
        TestFalse(FString::Printf(TEXT("Status %u has a catalog name"), uint32(Status)),
            LDHUD::StatusName(&Content->Get(), uint8(Status)).StartsWith(TEXT("未知")));
    }
    return true;
}

#endif
