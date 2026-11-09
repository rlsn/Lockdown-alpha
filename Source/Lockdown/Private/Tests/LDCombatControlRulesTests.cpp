#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDAttributeProjection.h"
#include "UI/LDHUDModel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>
#include "Tests/LDTestApprovedFixture.h"

namespace LDCombatControlTestDetail
{
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

    FLDDemoEffectDefinition Thorns()
    {
        FLDDemoEffectDefinition Effect;
        Effect.Opcode = uint8(ELDDemoEffectOp::Thorns);
        Effect.BaseP2 = 1500;
        Effect.ArmorRatioBp = 1500;
        Effect.RadiusCm = Rational(300);
        Effect.IntervalMs = 500;
        Effect.CriticalMultiplierBp = 20000;
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
    const EAction Actions[] = {EAction::Move, EAction::Displacement, EAction::Basic, EAction::Cast};
    // Expected matrix rows: Root, Disarm, Stun; columns follow Actions.
    const bool RootRow[] = {true, true, false, false};
    const bool DisarmRow[] = {false, false, true, false};
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
        | uint8(ELDCombatControlBlock::Displacement));
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
    const FLDDemoEffectDefinition Both = Thorns();
    const FLDDemoEffectDefinition BasicOnly = Thorns();
    const FGuid Attacker = FGuid::NewGuid(), Other = FGuid::NewGuid();
    const FString Key(TEXT("passive_thick_face"));
    TArray<FLDThornsMark> Marks;
    FLDThornsContact Basic;
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageP2 = 5000;
    Basic.DistanceCm = 200;
    TestTrue(TEXT("A primary basic hit in reach triggers"), LDCombatControl::ThornsTriggers(Both, Basic, Marks, Key, Attacker, 1000));
    FLDThornsContact Ability = Basic;
    Ability.HitClass = ELDCombatHitClass::Ability;
    TestFalse(TEXT("An ability hit never triggers a reflection"), LDCombatControl::ThornsTriggers(BasicOnly, Ability, Marks, Key, Attacker, 1000));
    FLDThornsContact Secondary = Basic;
    Secondary.HitClass = ELDCombatHitClass::BasicSecondary;
    TestFalse(TEXT("A cleave secondary never triggers"), LDCombatControl::ThornsTriggers(Both, Secondary, Marks, Key, Attacker, 1000));
    FLDThornsContact Reflected = Basic;
    Reflected.bReflected = true;
    TestFalse(TEXT("A reflection never triggers another reflection"), LDCombatControl::ThornsTriggers(Both, Reflected, Marks, Key, Attacker, 1000));
    FLDThornsContact Far = Basic;
    Far.DistanceCm = 300.5;
    TestFalse(TEXT("Beyond reach does not trigger"), LDCombatControl::ThornsTriggers(Both, Far, Marks, Key, Attacker, 1000));
    Far.DistanceCm = 300;
    TestTrue(TEXT("Exactly at reach triggers"), LDCombatControl::ThornsTriggers(Both, Far, Marks, Key, Attacker, 1000));
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
    TestTrue(TEXT("Base plus armor scaling"), LDCombatControl::ThornsBaseP2(Both, 4000, 0, false, Amount, Error) && Amount == 1500 + 4000 * 1500 / 10000);
    TestTrue(TEXT("A critical hit applies the critical multiplier"), LDCombatControl::ThornsBaseP2(Both, 4000, 0, true, Amount, Error) && Amount == (1500 + 600) * 2);
    FLDDemoEffectDefinition Health = Both;
    Health.MaxHPRatioBp = 150;
    TestTrue(TEXT("Armor and max-HP coefficients add (祖传铁裆 #112)"),
        LDCombatControl::ThornsBaseP2(Health, 4000, 100000, false, Amount, Error) && Amount == 1500 + 600 + 1500);
    TestTrue(TEXT("A multiplier of one leaves a critical reflection unchanged"),
        LDCombatControl::ThornsBaseP2(Thorns(), 0, 0, true, Amount, Error) && Amount == 1500 * 2);
    FLDDemoEffectDefinition Plain = BasicOnly;
    Plain.CriticalMultiplierBp = 10000;
    TestTrue(TEXT("No critical doubling without a multiplier"), LDCombatControl::ThornsBaseP2(Plain, 0, 0, true, Amount, Error) && Amount == 1500);
    TestFalse(TEXT("Negative armor is refused"), LDCombatControl::ThornsBaseP2(Both, -1, 0, false, Amount, Error));
    TestFalse(TEXT("Negative max HP is refused"), LDCombatControl::ThornsBaseP2(Health, 0, -1, false, Amount, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatExpansionContentTest, "Lockdown.Rules.DemoControl.ExpansionContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatExpansionContentTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControlTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDTestApprovedFixture::Read(*this, Content)) { return false; }
    FString Error;
    const FLDDemoAbilityDefinition* Unplug = nullptr;
    const FLDDemoAbilityDefinition* BailOut = nullptr;
    const FLDDemoAbilityDefinition* ThickFace = nullptr;
    if (!Content->FindAbility(TEXT("active_unplug"), Unplug, Error)
        || !Content->FindAbility(TEXT("active_bail_out"), BailOut, Error) || !Content->FindAbility(TEXT("passive_thick_face"), ThickFace, Error))
    {
        AddError(Error);
        return false;
    }
    for (const FLDDemoRankDefinition& Rank : Unplug->Ranks)
    {
        const FLDDemoEffectDefinition* Disarm = FindOp(Rank.Effects, ELDDemoEffectOp::Disarm);
        TestTrue(TEXT("Every unplug rank damages and disarms a unit"), Unplug->Target.Mode == ELDDemoTargetMode::Unit
            && FindOp(Rank.Effects, ELDDemoEffectOp::Damage) && Disarm && Disarm->DurationMs > 0);
    }
    for (const FLDDemoRankDefinition& Rank : BailOut->Ranks)
    {
        const FLDDemoEffectDefinition* Empower = FindOp(Rank.Effects, ELDDemoEffectOp::EmpowerBasic);
        TestTrue(TEXT("Every bail-out rank dashes and arms one empowered basic"), BailOut->Target.Mode == ELDDemoTargetMode::DirectionDash
            && FindOp(Rank.Effects, ELDDemoEffectOp::Dash) && Empower && Empower->DurationMs > 0
            && (Empower->BaseP2 > 0 || Empower->ADRatioBp > 0));
    }
    const FLDDemoEffectDefinition* FaceThorns = FindOp(ThickFace->Ranks.Last().Effects, ELDDemoEffectOp::Thorns);
    if (!TestTrue(TEXT("The reflection imports as typed thorns"), FaceThorns != nullptr)) { return false; }

    // A learned trigger passive projects as a rank-exact combat trigger, never an attribute term.
    const FLDAttributeBuildValues Build(3, {FLDLearnedPassiveValue(TEXT("passive_thick_face"), 2)}, {});
    const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(Build, *Content);
    if (!TestTrue(TEXT("The thorns build projects"), Projected.IsSuccess()))
    {
        AddError(Projected.Error.Field + TEXT(": ") + Projected.Error.Message);
        return false;
    }
    const FLDAttributeProjection& Projection = Projected.Value.GetValue();
    TestTrue(TEXT("The learned trigger keeps its rank"), Projection.LearnedTriggers.Num() == 1
        && Projection.LearnedTriggers[0].AbilityId == TEXT("passive_thick_face") && Projection.LearnedTriggers[0].Rank == 2);

    // Functional display: every new effect, target and status has explicit player text.
    for (const FLDDemoAbilityDefinition* Ability : {Unplug, BailOut, ThickFace})
    {
        for (const FLDDemoEffectDefinition& Effect : Ability->Ranks[0].Effects)
        {
            TestFalse(FString::Printf(TEXT("%s effect %u has a display line"), *Ability->Id, Effect.Opcode),
                LDHUD::EffectLine(Effect).StartsWith(TEXT("未知")));
        }
    }
    FLDDemoTargetDefinition Point;
    Point.Mode = ELDDemoTargetMode::GroundPoint;
    Point.RangeCm = Rational(400);
    TestFalse(TEXT("A ground point target has a display line"), LDHUD::TargetLine(Point).StartsWith(TEXT("未知")));
    for (const ELDDemoStatusId Status : {ELDDemoStatusId::Disarm, ELDDemoStatusId::Stun, ELDDemoStatusId::Empowered})
    {
        TestFalse(FString::Printf(TEXT("Status %u has a catalog name"), uint32(Status)),
            LDHUD::StatusName(&Content->Get(), uint8(Status)).StartsWith(TEXT("未知")));
    }
    return true;
}

#endif
