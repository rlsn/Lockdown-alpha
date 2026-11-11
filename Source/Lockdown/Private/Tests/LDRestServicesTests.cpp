#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

// #198 rest services (approved spec §4.4 NPC, §5 install/rebuild matrix): pure LDProgression rules only.
namespace
{
using ERestTestStage = LDProgression::EMatchStage;

bool RestTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

LDProgression::FBuildContext RestTestContext(ERestTestStage Stage, ELDLifePhase Phase, bool bSafe)
{
    LDProgression::FBuildContext Context;
    Context.Stage = Stage;
    Context.LifePhase = Phase;
    Context.bSafe = bSafe;
    return Context;
}

bool RestTestFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& OutCatalog, TArray<FName>& OutOpening,
    TArray<FName>& OutLater)
{
    if (!LDTestApprovedFixture::Read(Test, OutCatalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = OutCatalog->Get();
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        (Ability.Opening ? OutOpening : OutLater).Add(FName(*Ability.Id));
    }
    bool bGood = Test.TestTrue(TEXT("Fixture carries the approved NPC price of 3 points and 3 opening points"),
        Content.Parameters.NpcActivePoints == 3 && Content.Parameters.OpeningPoints == 3);
    bGood &= Test.TestTrue(TEXT("Fixture carries at least six opening actives and three later-only actives"),
        OutOpening.Num() >= 6 && OutLater.Num() >= 3);
    bGood &= Test.TestTrue(TEXT("Fixture carries the five point-bought stats"), Content.Stats.Num() == 5);
    return bGood;
}

FLDGrowthBuild RestTestBuild(const FLDDemoContentDefinitions& Content, uint8 Level, TArray<FLDLearnedActive> Actives,
    TArray<FName> Blueprints = {}, TArray<int32> Points = {}, int64 Permanent = 0)
{
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = Level;
    Build.Actives = MoveTemp(Actives);
    Build.PendingBlueprints = MoveTemp(Blueprints);
    if (!Points.IsEmpty())
    {
        Build.StatPoints = MoveTemp(Points);
    }
    Build.PermanentSpentPoints = Permanent;
    LDProgression::NormalizeBuild(Build);
    return Build;
}

// The opening opportunity chosen and nothing queued: the state in which the NPC may sell one.
FLDProgressionState RestTestSettled(int64 XP)
{
    FLDProgressionState Progression;
    Progression.CumulativeXP = XP;
    Progression.ActiveEarned = 1;
    Progression.ActiveConsumed = 1;
    Progression.LastOfferRevision = 1;
    return Progression;
}

FLDActiveOfferState RestTestOffer(uint8 Ordinal, TArray<FName> Options)
{
    FLDActiveOfferState Offer;
    Offer.OfferId = FGuid(0x198C, Ordinal, 0, 1);
    Offer.Ordinal = Ordinal;
    Offer.Options = MoveTemp(Options);
    Offer.Revision = 2;
    return Offer;
}

int64 RestTestUnspent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, int64 XP)
{
    int64 Unspent = -1;
    return LDProgression::UnspentPoints(Content, Build, XP, 0, Unspent) == ELDGrowthReason::None ? Unspent : -1;
}

const TCHAR* RestTestKindName(ELDBuildCommandKind Kind)
{
    switch (Kind)
    {
    case ELDBuildCommandKind::ActiveChoice: return TEXT("ActiveChoice");
    case ELDBuildCommandKind::LearnBlueprint: return TEXT("LearnBlueprint");
    case ELDBuildCommandKind::AllocatePoints: return TEXT("AllocatePoints");
    case ELDBuildCommandKind::BuyNpcOffer: return TEXT("BuyNpcOffer");
    case ELDBuildCommandKind::Respec: return TEXT("Respec");
    case ELDBuildCommandKind::ReplaceActive: return TEXT("ReplaceActive");
    case ELDBuildCommandKind::ForgetActive: return TEXT("ForgetActive");
    default: return TEXT("Unknown");
    }
}

bool RestTestRunGates(FAutomationTestBase& Test)
{
    using EKind = ELDBuildCommandKind;
    using EReason = ELDGrowthReason;
    const EKind Kinds[] = {EKind::ActiveChoice, EKind::LearnBlueprint, EKind::AllocatePoints, EKind::BuyNpcOffer, EKind::Respec,
        EKind::ReplaceActive, EKind::ForgetActive};
    struct FRow
    {
        const TCHAR* What;
        LDProgression::FBuildContext Context;
        bool bRest;
        EReason Expected[7];
    };
    const EReason NotRest = EReason::NotRestWindow;
    const EReason NoReplace = EReason::ReplacementNotAllowed;
    const EReason Locked = EReason::BuildLocked;
    const EReason Closed = EReason::PhaseClosed;
    const FRow Matrix[] = {
        {TEXT("field, alive and safe"), RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, true), false,
            {EReason::None, EReason::None, EReason::None, NotRest, NotRest, NoReplace, NoReplace}},
        {TEXT("field, alive inside the safe window"), RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, false), false,
            {EReason::None, EReason::NotSafe, EReason::NotSafe, NotRest, NotRest, NoReplace, NoReplace}},
        {TEXT("field, dead"), RestTestContext(ERestTestStage::Live, ELDLifePhase::Dead, false), false,
            {EReason::None, EReason::Dead, EReason::None, NotRest, NotRest, NoReplace, NoReplace}},
        {TEXT("rest window, alive inside the safe window"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false), true,
            {EReason::None, EReason::None, EReason::None, EReason::None, EReason::None, EReason::None, EReason::None}},
        {TEXT("rest window, dead"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Dead, true), true,
            {EReason::None, EReason::Dead, EReason::None, EReason::Dead, EReason::Dead, EReason::Dead, EReason::Dead}},
        {TEXT("rest step with builds locked"), RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive, true), false,
            {Locked, Locked, Locked, Locked, Locked, Locked, Locked}},
        {TEXT("closed"), RestTestContext(ERestTestStage::Closed, ELDLifePhase::Alive, true), false,
            {Closed, Closed, Closed, Closed, Closed, Closed, Closed}},
        {TEXT("unspawned in the rest window"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Unspawned, true), true,
            {EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged,
             EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged}}};
    bool bGood = true;
    constexpr int32 KindCount = UE_ARRAY_COUNT(Kinds);
    for (const FRow& Row : Matrix)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("IsRestWindow is %s, %s"), Row.bRest ? TEXT("true") : TEXT("false"), Row.What),
            LDProgression::IsRestWindow(Row.Context) == Row.bRest);
        for (int32 Index = 0; Index < KindCount; ++Index)
        {
            bGood &= RestTestReason(Test, FString::Printf(TEXT("%s, %s"), RestTestKindName(Kinds[Index]), Row.What),
                LDProgression::CommandGate(Kinds[Index], Row.Context), Row.Expected[Index]);
        }
    }
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, true);
    bGood &= RestTestReason(Test, TEXT("LearnGate in the rest window ignores the safe window"),
        LDProgression::LearnGate(RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false)), EReason::None);
    bGood &= RestTestReason(Test, TEXT("LearnGate while builds are locked"),
        LDProgression::LearnGate(RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive, true)), EReason::BuildLocked);
    for (const uint8 Unknown : {uint8(4), uint8(19), uint8(26), uint8(255)})
    {
        bGood &= RestTestReason(Test, FString::Printf(TEXT("Command kind %u is refused, not mapped to a neighbour"), uint32(Unknown)),
            LDProgression::CommandGate(static_cast<EKind>(Unknown), Rest), EReason::InvalidRequest);
    }

    const TPair<EKind, uint8> KindNumbers[] = {{EKind::BuyNpcOffer, 20}, {EKind::Respec, 21}, {EKind::ReplaceActive, 22},
        {EKind::ForgetActive, 23}};
    for (const TPair<EKind, uint8>& Row : KindNumbers)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("%s travels as %u"), RestTestKindName(Row.Key), uint32(Row.Value)),
            int32(static_cast<uint8>(Row.Key)), int32(Row.Value));
    }
    const TPair<EReason, uint16> ReasonNumbers[] = {{EReason::ReplacementNotAllowed, 48}, {EReason::BuildLocked, 70},
        {EReason::NpcPoolTooSmall, 71}, {EReason::OfferOutstanding, 72}, {EReason::NothingToRespec, 73}, {EReason::SlotEmpty, 74},
        {EReason::NotRestWindow, 75}, {EReason::RestServiceUnavailable, 76}};
    for (const TPair<EReason, uint16>& Row : ReasonNumbers)
    {
        const FString Name = LDGrowthWire::ReasonName(Row.Key);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as %u and decodes to its own name"), *Name, uint32(Row.Value)),
            LDGrowthWire::Raw(Row.Key) == Row.Value && Name != TEXT("Unknown")
            && Name == LDGrowthWire::ReasonName(LDGrowthWire::Reason(Row.Value)));
    }
    for (const uint16 Unassigned : {uint16(77)})
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Unassigned growth reason %u has no name"), uint32(Unassigned)),
            FString(LDGrowthWire::ReasonName(LDGrowthWire::Reason(Unassigned))), FString(TEXT("Unknown")));
    }
    return bGood;
}

bool RestTestRunNpc(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(Test, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const int64 Cost = Content.Parameters.NpcActivePoints;
    const FGuid Secret(0x198C, 1, 2, 3);
    const FGuid Contender(0x198C, 4, 5, 6);
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false);
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    LDProgression::FNpcOfferOutcome Outcome;
    const auto Buy = [&](const FLDGrowthBuild& From, const FLDProgressionState& Progression, const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyBuyNpcOffer(Content, From, Progression, Secret, Contender, Context, Out, OutProgression, Outcome);
    };
    const auto Uncharged = [&](const TCHAR* What, ELDGrowthReason Actual, ELDGrowthReason Expected, const FLDGrowthBuild& From,
        const FLDProgressionState& Progression)
    {
        bool bGood = RestTestReason(Test, What, Actual, Expected);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s charges nothing and opens no opportunity"), What),
            Out.PermanentSpentPoints == From.PermanentSpentPoints && Out.StatPoints == From.StatPoints
            && OutProgression.ActiveEarned == Progression.ActiveEarned && OutProgression.NpcPurchased == Progression.NpcPurchased
            && OutProgression.LastOfferRevision == Progression.LastOfferRevision
            && OutProgression.Offer.IsSet() == Progression.Offer.IsSet() && Outcome.PointsPaid == 0);
        return bGood;
    };

    // Every opening active learned or kept as a blueprint: only the later-only actives remain, so a drawn offer can only
    // come from the whole pool.
    TArray<FLDLearnedActive> Learned = {{Opening[0], 0}, {Opening[1], 1}, {Opening[2], 2}};
    TArray<FName> OpeningBlueprints(Opening.GetData() + 3, Opening.Num() - 3);
    const FLDGrowthBuild OnlyLater = RestTestBuild(Content, 1, Learned, OpeningBlueprints);
    const FLDProgressionState Settled = RestTestSettled(0);
    bool bGood = RestTestReason(Test, TEXT("The NPC sells with exactly its price unspent"), Buy(OnlyLater, Settled, Rest),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The payment is exactly GROWTH.NpcActivePoints, permanently spent"),
        Outcome.PointsPaid == Cost && Out.PermanentSpentPoints == Cost && RestTestUnspent(Content, Out, 0) == 3 - Cost
        && Out.StatPoints == OnlyLater.StatPoints && Out.Actives.Num() == 3 && Out.PendingBlueprints == OnlyLater.PendingBlueprints);
    bGood &= Test.TestTrue(TEXT("One new opportunity is earned and its offer is fixed at payment"),
        Outcome.Ordinal == 2 && OutProgression.ActiveEarned == 2 && OutProgression.ActiveConsumed == 1
        && OutProgression.NpcPurchased == 1 && OutProgression.QueuedActiveOpportunities() == 1 && OutProgression.Offer.IsSet()
        && OutProgression.Offer->Ordinal == 2 && OutProgression.Offer->OfferId.IsValid()
        && OutProgression.Offer->Revision == 2 && OutProgression.LastOfferRevision == 2);
    if (OutProgression.Offer.IsSet())
    {
        const TArray<FName>& Options = OutProgression.Offer->Options;
        TArray<FName> Expected;
        LDProgression::DrawActiveOffer(Content, OnlyLater, 2, LDProgression::OfferSeed(Secret, Contender, 2), Expected);
        bGood &= Test.TestTrue(TEXT("The offer holds three distinct later-only actives (the whole pool, not the opening one)"),
            Options.Num() == 3 && Options[0] != Options[1] && Options[0] != Options[2] && Options[1] != Options[2]
            && Options.ContainsByPredicate([&](FName Id) { return Later.Contains(Id); })
            && !Options.ContainsByPredicate([&](FName Id) { return Opening.Contains(Id); }));
        bGood &= Test.TestTrue(TEXT("The offer is the deterministic draw of the bought ordinal's seed"), Options == Expected);
    }

    // The same purchase on a build with every eligible active offered as well: the draw is uniform without replacement.
    const FLDGrowthBuild Fresh = RestTestBuild(Content, 1, {{Opening[0], 0}});
    bGood &= Test.TestTrue(TEXT("A fresh build buys three distinct actives none of which it holds"),
        Buy(Fresh, Settled, Rest) == ELDGrowthReason::None && OutProgression.Offer.IsSet() && OutProgression.Offer->Options.Num() == 3
        && !OutProgression.Offer->Options.Contains(Opening[0]));

    // Refusals never charge.
    FLDProgressionState Offered = Settled;
    Offered.ActiveEarned = 2;
    Offered.Offer.Emplace(RestTestOffer(2, {Later[0], Later[1], Later[2]}));
    bGood &= Uncharged(TEXT("A purchase while an active offer is outstanding"), Buy(Fresh, Offered, Rest),
        ELDGrowthReason::OfferOutstanding, Fresh, Offered);
    FLDProgressionState Waiting = Settled;
    Waiting.ActiveEarned = 2;
    bGood &= Uncharged(TEXT("A purchase while an opportunity waits for an eligible active"), Buy(Fresh, Waiting, Rest),
        ELDGrowthReason::OfferOutstanding, Fresh, Waiting);
    TArray<FName> MostBlueprints = OpeningBlueprints;
    for (int32 Index = 0; Index < Later.Num() - 2; ++Index)
    {
        MostBlueprints.Add(Later[Index]);
    }
    const FLDGrowthBuild TwoLeft = RestTestBuild(Content, 1, Learned, MostBlueprints);
    bGood &= Uncharged(TEXT("A purchase with only two eligible actives"), Buy(TwoLeft, Settled, Rest), ELDGrowthReason::NpcPoolTooSmall,
        TwoLeft, Settled);
    const FLDGrowthBuild Short = RestTestBuild(Content, 1, {{Opening[0], 0}}, {}, {1, 0, 0, 0, 0});
    bGood &= Uncharged(TEXT("A purchase one point short"), Buy(Short, Settled, Rest), ELDGrowthReason::NotEnoughPoints, Short, Settled);
    const FLDGrowthBuild Paid = RestTestBuild(Content, 1, {{Opening[0], 0}}, {}, {}, Cost);
    bGood &= Uncharged(TEXT("A second purchase with the points already paid"), Buy(Paid, Settled, Rest), ELDGrowthReason::NotEnoughPoints,
        Paid, Settled);
    bGood &= Uncharged(TEXT("A purchase on the field"), Buy(Fresh, Settled, RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, true)),
        ELDGrowthReason::NotRestWindow, Fresh, Settled);
    bGood &= Uncharged(TEXT("A purchase while builds are locked"),
        Buy(Fresh, Settled, RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive, true)), ELDGrowthReason::BuildLocked,
        Fresh, Settled);

    // The bought opportunity stays an opportunity of its own when the level adds the next one.
    LDProgression::FExperienceChange Change;
    FLDProgressionState Bought = Settled;
    Bought.ActiveEarned = 2;
    Bought.NpcPurchased = 1;
    bGood &= Test.TestTrue(TEXT("XP plans see only the level part of the earned opportunities"),
        LDProgression::PlanExperience(Content.Progression, 0, 1, uint8(Bought.ActiveEarned - Bought.NpcPurchased), 180, Change)
            == ELDGrowthReason::None
        && Change.LevelAfter == 3 && int32(Change.ActiveEarnedAfter) + Bought.NpcPurchased == 3);
    return bGood;
}

bool RestTestRunRespec(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(Test, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const int64 Cost = Content.Parameters.NpcActivePoints;
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false);
    FLDGrowthBuild Out;
    int64 Refunded = -1;
    // Level 3 at 180 XP earned 3 + 2 x 3 = 9 points: 6 allocated and the NPC price paid.
    const int64 XP = 180;
    const FLDGrowthBuild Built = RestTestBuild(Content, 3, {{Opening[0], 0}}, {}, {2, 0, 3, 0, 1}, Cost);
    const int64 Before = RestTestUnspent(Content, Built, XP);
    bool bGood = Test.TestEqual(TEXT("Before the respec, earned minus allocated minus paid is unspent"), Before, 9 - 6 - Cost);
    bGood &= RestTestReason(Test, TEXT("A respec in the rest window"),
        LDProgression::ApplyRespec(Content, Built, XP, 0, Rest, Out, Refunded), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Only the allocated points return; the permanent spend stays"),
        Refunded == 6 && Out.StatPoints == TArray<int32>({0, 0, 0, 0, 0}) && Out.PermanentSpentPoints == Cost
        && RestTestUnspent(Content, Out, XP) == 9 - Cost && Out.Actives.Num() == 1 && Out.Level == 3 && Out.Revision == Built.Revision);
    const FLDGrowthBuild Respecced = Out;
    bGood &= RestTestReason(Test, TEXT("A respec with nothing allocated"),
        LDProgression::ApplyRespec(Content, Respecced, XP, 0, Rest, Out, Refunded), ELDGrowthReason::NothingToRespec);
    bGood &= Test.TestTrue(TEXT("A refused respec refunds nothing"), Refunded == 0 && Out.PermanentSpentPoints == Cost);
    bGood &= RestTestReason(Test, TEXT("A respec on the field"),
        LDProgression::ApplyRespec(Content, Built, XP, 0, RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, true), Out, Refunded),
        ELDGrowthReason::NotRestWindow);
    bGood &= Test.TestTrue(TEXT("A respec refused on the field changes nothing"), Refunded == 0 && Out.StatPoints == Built.StatPoints);
    bGood &= RestTestReason(Test, TEXT("A respec while builds are locked"),
        LDProgression::ApplyRespec(Content, Built, XP, 0, RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive, true), Out,
            Refunded), ELDGrowthReason::BuildLocked);
    bGood &= RestTestReason(Test, TEXT("A respec of a build that paid more than it earned"),
        LDProgression::ApplyRespec(Content, RestTestBuild(Content, 1, {}, {}, {1, 0, 0, 0, 0}, 3), 0, 0, Rest, Out, Refunded),
        ELDGrowthReason::AuthorityFault);
    bGood &= RestTestReason(Test, TEXT("A negative permanent spend is a broken record"),
        LDProgression::ValidateBuild(Content, RestTestBuild(Content, 1, {}, {}, {}, -1)), ELDGrowthReason::DataInvalid);
    return bGood;
}

bool RestTestRunReplaceForget(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(Test, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FName A = Opening[0], B = Opening[1], C = Opening[2], D = Opening[3], E = Opening[4];
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false);
    const LDProgression::FBuildContext Field = RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, true);
    const LDProgression::FBuildContext Locked = RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive, true);
    const FLDGrowthBuild Full = RestTestBuild(Content, 6, {{A, 0}, {B, 1}, {C, 2}}, {D});
    const FLDProgressionState Settled = RestTestSettled(600);
    FLDProgressionState Offered = Settled;
    Offered.ActiveEarned = 2;
    Offered.Offer.Emplace(RestTestOffer(2, {Later[0], Later[1], Later[2]}));
    FLDGrowthBuild Out;
    FName Forgotten;
    TArray<FName> Reclaimed;
    const auto Replace = [&](const FLDGrowthBuild& From, const FLDProgressionState& Progression, FName Blueprint, uint8 Slot,
        const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyReplaceActive(Content, From, Progression, Blueprint, Slot, Context, Out, Forgotten, Reclaimed);
    };
    const auto Forget = [&](const FLDGrowthBuild& From, const FLDProgressionState& Progression, uint8 Slot,
        const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyForgetActive(Content, From, Progression, Slot, Context, Out, Forgotten, Reclaimed);
    };
    const auto Unchanged = [&](const TCHAR* What, ELDGrowthReason Actual, ELDGrowthReason Expected, const FLDGrowthBuild& From)
    {
        bool bGood = RestTestReason(Test, What, Actual, Expected);
        bool bSame = Out.Actives.Num() == From.Actives.Num() && Out.PendingBlueprints == From.PendingBlueprints && Forgotten.IsNone()
            && Reclaimed.IsEmpty();
        for (int32 Index = 0; bSame && Index < From.Actives.Num(); ++Index)
        {
            bSame = Out.Actives[Index].Id == From.Actives[Index].Id && Out.Actives[Index].Slot == From.Actives[Index].Slot;
        }
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s changes nothing"), What), bSame);
        return bGood;
    };
    TArray<FName> Eligible;

    // ---- ReplaceActive: the blueprint is consumed into the occupied slot and the old active is forgotten ----
    bool bGood = RestTestReason(Test, TEXT("Replacing W with a pending blueprint in the rest window"), Replace(Full, Settled, D, 1, Rest),
        ELDGrowthReason::None);
    const int32 InW = Out.ActiveInSlot(1);
    bGood &= Test.TestTrue(TEXT("The blueprint is learned in W, consumed, and the old active neither kept nor stored"),
        Forgotten == B && InW != INDEX_NONE && Out.Actives[InW].Id == D && Out.Actives.Num() == 3 && Out.PendingBlueprints.IsEmpty()
        && !Out.FindActive(B) && !Out.HasBlueprint(B) && Out.StatPoints == Full.StatPoints && Out.Revision == Full.Revision
        && LDProgression::ValidateBuild(Content, Out) == ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The replaced active is eligible for later offers again"),
        LDProgression::EligibleActives(Content, Out, 2, Eligible) == ELDGrowthReason::None && Eligible.Contains(B) && !Eligible.Contains(D));
    bGood &= Unchanged(TEXT("Replacing on the field"), Replace(Full, Settled, D, 1, Field), ELDGrowthReason::ReplacementNotAllowed, Full);
    bGood &= Unchanged(TEXT("Replacing while dead on the field"),
        Replace(Full, Settled, D, 1, RestTestContext(ERestTestStage::Live, ELDLifePhase::Dead, false)),
        ELDGrowthReason::ReplacementNotAllowed, Full);
    bGood &= Unchanged(TEXT("Replacing while builds are locked"), Replace(Full, Settled, D, 1, Locked), ELDGrowthReason::BuildLocked, Full);
    bGood &= Unchanged(TEXT("Replacing while an active offer is outstanding"), Replace(Full, Offered, D, 1, Rest),
        ELDGrowthReason::OfferOutstanding, Full);
    bGood &= Unchanged(TEXT("Replacing with an active that is no pending blueprint"), Replace(Full, Settled, E, 1, Rest),
        ELDGrowthReason::NoBlueprint, Full);
    const FLDGrowthBuild Partial = RestTestBuild(Content, 6, {{A, 0}}, {D});
    bGood &= Unchanged(TEXT("Replacing into an empty slot"), Replace(Partial, Settled, D, 2, Rest), ELDGrowthReason::SlotEmpty, Partial);
    bGood &= Unchanged(TEXT("Replacing into a slot past E"), Replace(Full, Settled, D, 3, Rest), ELDGrowthReason::InvalidRequest, Full);
    FLDGrowthBuild Learned;
    bGood &= RestTestReason(Test, TEXT("Learning a blueprint into an occupied slot is still no replacement in the rest window"),
        LDProgression::ApplyLearnBlueprint(Content, Full, D, 0, Rest, Learned), ELDGrowthReason::SlotOccupied);
    bGood &= Test.TestTrue(TEXT("An empty slot is learned by LearnBlueprint in the rest window"),
        LDProgression::ApplyLearnBlueprint(Content, Partial, D, 2, Rest, Learned) == ELDGrowthReason::None
        && Learned.ActiveInSlot(2) != INDEX_NONE && Learned.PendingBlueprints.IsEmpty());

    // ---- ForgetActive: the slot empties and the active needs a new source to come back ----
    bGood &= RestTestReason(Test, TEXT("Forgetting Q in the rest window"), Forget(Full, Settled, 0, Rest), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Q is empty, the active is forgotten and not stored as a blueprint"),
        Forgotten == A && Out.ActiveInSlot(0) == INDEX_NONE && Out.Actives.Num() == 2 && !Out.FindActive(A) && !Out.HasBlueprint(A)
        && Out.PendingBlueprints == Full.PendingBlueprints && Out.Revision == Full.Revision
        && LDProgression::ValidateBuild(Content, Out) == ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The forgotten active is eligible for later offers and the NPC again"),
        LDProgression::EligibleActives(Content, Out, 2, Eligible) == ELDGrowthReason::None && Eligible.Contains(A));
    bGood &= Unchanged(TEXT("Forgetting on the field"), Forget(Full, Settled, 0, Field), ELDGrowthReason::ReplacementNotAllowed, Full);
    bGood &= Unchanged(TEXT("Forgetting while builds are locked"), Forget(Full, Settled, 0, Locked), ELDGrowthReason::BuildLocked, Full);
    bGood &= Unchanged(TEXT("Forgetting while an active offer is outstanding"), Forget(Full, Offered, 0, Rest),
        ELDGrowthReason::OfferOutstanding, Full);
    bGood &= Unchanged(TEXT("Forgetting an empty slot"), Forget(Partial, Settled, 1, Rest), ELDGrowthReason::SlotEmpty, Partial);
    bGood &= Unchanged(TEXT("Forgetting a slot past E"), Forget(Full, Settled, 3, Rest), ELDGrowthReason::InvalidRequest, Full);
    return bGood;
}

FLDComponentInstance RestTestInstalled(FName Id, uint8 Level, ELDComponentTargetKind Kind = ELDComponentTargetKind::None,
    FName Definition = NAME_None, FName BoundActive = NAME_None, ELDDemoElement Element = ELDDemoElement::None,
    FName BoundComponent = NAME_None)
{
    FLDComponentInstance Instance;
    Instance.Id = Id;
    Instance.Source = ELDComponentSource::LevelUp;
    Instance.SourceLevel = Level;
    Instance.bInstalled = Instance.bEverInstalled = true;
    Instance.Target.Kind = Kind;
    Instance.Target.Definition = Definition;
    Instance.Target.BoundActive = BoundActive;
    Instance.Target.Element = Element;
    Instance.Target.BoundComponent = BoundComponent;
    return Instance;
}

// #198 §5 (换下即遗忘，挂件退库存或迁往合法对象，不复制、不丢失、不自动错绑): forgetting or replacing an active returns
// what sat on it (a mod on it, a B18 bound to it, a connector on that B18) to the inventory in the same commit, leaves every
// other install alone, binds nothing to the new active, and a later move is the player's own rest-window install.
bool RestTestRunForgetReclaim(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(Test, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    using EKind = ELDComponentTargetKind;
    const FName Elbow(TEXT("active_elbow")), BailOut(TEXT("active_bail_out")), Zap(TEXT("active_zap")), Seethe(TEXT("active_seethe"));
    const FName Trick(TEXT("component_dirty_trick")), Chase(TEXT("component_blast_chase")), Crowd(TEXT("component_crowd_hit"));
    const FName Water(TEXT("component_fist_water")), DirtyFist(TEXT("component_dirty_fist")), Again(TEXT("component_again_now"));
    const FName Spiky(TEXT("component_spiky_hide"));
    FLDGrowthBuild Built = RestTestBuild(Content, 8, {{Elbow, 0}, {BailOut, 1}, {Zap, 2}}, {Seethe});
    FLDComponentInstance Stored;
    Stored.Id = Spiky;
    Stored.Source = ELDComponentSource::LevelUp;
    Stored.SourceLevel = 8;
    Built.Components = {RestTestInstalled(Trick, 2, EKind::None, NAME_None, Elbow, ELDDemoElement::Fire),
        RestTestInstalled(Chase, 3, EKind::Component, Trick), RestTestInstalled(Crowd, 4, EKind::Active, Elbow),
        RestTestInstalled(Water, 5), RestTestInstalled(DirtyFist, 6, EKind::Active, Zap, NAME_None, ELDDemoElement::None, Water),
        RestTestInstalled(Again, 7, EKind::Active, BailOut), Stored};
    LDProgression::NormalizeBuild(Built);
    if (!RestTestReason(Test, TEXT("The reclaim fixture build"), LDProgression::ValidateBuild(Content, Built), ELDGrowthReason::None))
    {
        return false;
    }
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive, false);
    const LDProgression::FBuildContext Field = RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive, true);
    const FLDProgressionState Settled = RestTestSettled(2000);
    FLDGrowthBuild Out;
    FName Forgotten;
    TArray<FName> Reclaimed;
    // The ids that left an install, in id order, must equal Expected; everything else keeps its exact install state.
    const auto Exactly = [&](const TCHAR* What, const TArray<FName>& Expected)
    {
        bool bSame = Out.Components.Num() == Built.Components.Num();
        for (int32 Index = 0; bSame && Index < Built.Components.Num(); ++Index)
        {
            const FLDComponentInstance& Before = Built.Components[Index];
            const FLDComponentInstance& After = Out.Components[Index];
            bSame = After.Id == Before.Id && After.Source == Before.Source && After.SourceLevel == Before.SourceLevel
                && After.bEverInstalled == Before.bEverInstalled
                && (Expected.Contains(Before.Id) ? !After.bInstalled && After.Target.IsEmpty()
                    : After.bInstalled == Before.bInstalled && After.Target == Before.Target);
        }
        bool bGood = Test.TestTrue(FString::Printf(TEXT("%s: the reclaimed list"), What), Reclaimed == Expected);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s: reclaimed to the inventory with their history, the rest untouched, none lost or doubled"),
            What), bSame);
        bGood &= RestTestReason(Test, FString::Printf(TEXT("%s: the committed build is valid"), What), LDProgression::ValidateBuild(Content, Out),
            ELDGrowthReason::None);
        return bGood;
    };

    bool bGood = RestTestReason(Test, TEXT("Forgetting Q (elbow)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 0, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting elbow"), {Chase, Crowd, Trick});
    bGood &= RestTestReason(Test, TEXT("Replacing Q (elbow) with seethe"),
        LDProgression::ApplyReplaceActive(Content, Built, Settled, Seethe, 0, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Replacing elbow"), {Chase, Crowd, Trick});
    bGood &= Test.TestTrue(TEXT("Nothing binds to the new active on its own"), Forgotten == Elbow && Out.ActiveInSlot(0) != INDEX_NONE
        && Out.Actives[Out.ActiveInSlot(0)].Id == Seethe && !Out.Components.ContainsByPredicate([&Seethe](const FLDComponentInstance& Row)
            { return Row.Target.Definition == Seethe || Row.Target.BoundActive == Seethe; }));
    const FLDGrowthBuild Replaced = Out;
    FLDComponentTarget OnZap;
    OnZap.BoundActive = Zap;
    OnZap.Element = ELDDemoElement::Water;
    FLDGrowthBuild Moved;
    bGood &= RestTestReason(Test, TEXT("A reclaimed B18 is not re-installed on the field (a move)"),
        LDProgression::ApplyInstallComponent(Content, Replaced, Trick, OnZap, Field, Moved), ELDGrowthReason::RestOnly);
    bGood &= Test.TestTrue(TEXT("The player moves the reclaimed B18 to another legal active in the rest window"),
        LDProgression::ApplyInstallComponent(Content, Replaced, Trick, OnZap, Rest, Moved) == ELDGrowthReason::None
        && Moved.FindComponent(Trick)->bInstalled && Moved.FindComponent(Trick)->Target == OnZap
        && LDProgression::ValidateBuild(Content, Moved) == ELDGrowthReason::None);

    bGood &= RestTestReason(Test, TEXT("Forgetting E (zap)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 2, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting zap"), {DirtyFist});
    bGood &= RestTestReason(Test, TEXT("Forgetting W (bail out)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 1, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting bail out"), {Again});

    // A build that kept an install on a lost target is a broken record, never a quiet state.
    FLDGrowthBuild Lost = Built;
    Lost.Actives.RemoveAt(Lost.ActiveInSlot(0));
    bGood &= RestTestReason(Test, TEXT("An install left on a forgotten active"), LDProgression::ValidateBuild(Content, Lost),
        ELDGrowthReason::DataInvalid);
    FLDGrowthBuild Orphan = Built;
    Orphan.FindComponent(Trick)->bInstalled = false;
    Orphan.FindComponent(Trick)->Target = FLDComponentTarget();
    bGood &= RestTestReason(Test, TEXT("A connector left on an uninstalled host"), LDProgression::ValidateBuild(Content, Orphan),
        ELDGrowthReason::DataInvalid);
    return bGood;
}
}

// #198 rest services: the Live/Rest/RestLocked/Closed legality matrix of every build command kind, IsRestWindow and the
// wire numbers of the rest kinds and reasons.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoRestServicesGatesTest, "Lockdown.Rules.DemoRest.Services.Gates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoRestServicesGatesTest::RunTest(const FString& Parameters)
{
    return RestTestRunGates(*this);
}

// #198 §4.4: the NPC charges exactly GROWTH.NpcActivePoints for one opportunity whose three-active offer of the whole pool
// is fixed at payment; an outstanding offer or queued opportunity, a pool under three and missing points charge nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoRestServicesNpcTest, "Lockdown.Rules.DemoRest.Services.Npc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoRestServicesNpcTest::RunTest(const FString& Parameters)
{
    return RestTestRunNpc(*this);
}

// #198 §5: a respec returns only the allocated points; the permanent NPC spend stays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoRestServicesRespecTest, "Lockdown.Rules.DemoRest.Services.Respec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoRestServicesRespecTest::RunTest(const FString& Parameters)
{
    return RestTestRunRespec(*this);
}

// #198 §5: replacing consumes a blueprint and forgets the old active; forgetting empties the slot and frees the active
// for later offers; both only in the rest window and never while an active offer is outstanding.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoRestServicesReplaceForgetTest, "Lockdown.Rules.DemoRest.Services.ReplaceForget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoRestServicesReplaceForgetTest::RunTest(const FString& Parameters)
{
    return RestTestRunReplaceForget(*this);
}

// #198 §5: forgetting or replacing an active reclaims its attachments to the inventory in the same commit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoRestServicesForgetReclaimTest, "Lockdown.Rules.DemoRest.Services.ForgetReclaim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoRestServicesForgetReclaimTest::RunTest(const FString& Parameters)
{
    return RestTestRunForgetReclaim(*this);
}

#endif
