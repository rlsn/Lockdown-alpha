#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"

// #198 rest services (approved spec §4.4 NPC, §5 install/rebuild matrix): pure LDProgression rules only.
namespace
{
using ERestTestStage = LDProgression::EMatchStage;

bool RestTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

LDProgression::FBuildContext RestTestContext(ERestTestStage Stage, ELDLifePhase Phase)
{
    LDProgression::FBuildContext Context;
    Context.Stage = Stage;
    Context.LifePhase = Phase;
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
    bool bGood = Test.TestTrue(TEXT("Fixture carries the opening points"), Content.Parameters.OpeningPoints == 3);
    bGood &= Test.TestTrue(TEXT("Fixture carries at least six opening actives and three later-only actives"),
        OutOpening.Num() >= 6 && OutLater.Num() >= 3);
    bGood &= Test.TestTrue(TEXT("Fixture carries the five point-bought stats"), Content.Stats.Num() == 5);
    return bGood;
}

FLDGrowthBuild RestTestBuild(const FLDDemoContentDefinitions& Content, uint8 Level, TArray<FLDLearnedActive> Actives,
    TArray<FName> Blueprints = {}, TArray<int32> Points = {})
{
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = Level;
    Build.Actives = MoveTemp(Actives);
    Build.PendingBlueprints = MoveTemp(Blueprints);
    if (!Points.IsEmpty())
    {
        Build.StatPoints = MoveTemp(Points);
    }
    LDProgression::NormalizeBuild(Build);
    return Build;
}

// The opening opportunity chosen and nothing queued.
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
    return LDProgression::UnspentPoints(Content, Build, XP, Unspent) == ELDGrowthReason::None ? Unspent : -1;
}

const TCHAR* RestTestKindName(ELDBuildCommandKind Kind)
{
    switch (Kind)
    {
    case ELDBuildCommandKind::ActiveChoice: return TEXT("ActiveChoice");
    case ELDBuildCommandKind::LearnBlueprint: return TEXT("LearnBlueprint");
    case ELDBuildCommandKind::AllocatePoints: return TEXT("AllocatePoints");
    case ELDBuildCommandKind::ShopBuy: return TEXT("ShopBuy");
    case ELDBuildCommandKind::ShopRefresh: return TEXT("ShopRefresh");
    case ELDBuildCommandKind::ShopSell: return TEXT("ShopSell");
    case ELDBuildCommandKind::UsePickTicket: return TEXT("UsePickTicket");
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
    // Spec §3.13/§11.3: the shop keeps the rest window like the trainer (without its NPC); a pick ticket is open like the build.
    const EKind Kinds[] = {EKind::ActiveChoice, EKind::LearnBlueprint, EKind::AllocatePoints, EKind::ShopBuy, EKind::Respec,
        EKind::ReplaceActive, EKind::ForgetActive, EKind::ShopRefresh, EKind::ShopSell, EKind::UsePickTicket};
    struct FRow
    {
        const TCHAR* What;
        LDProgression::FBuildContext Context;
        bool bRest;
        EReason Expected[10];
    };
    const EReason NotRest = EReason::NotRestWindow;
    const EReason NoReplace = EReason::ReplacementNotAllowed;
    const EReason Locked = EReason::BuildLocked;
    const EReason Closed = EReason::PhaseClosed;
    const FRow Matrix[] = {
        {TEXT("field, alive (in combat or not)"), RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive), false,
            {EReason::None, EReason::None, EReason::None, NotRest, NotRest, NoReplace, NoReplace, NotRest, NotRest, EReason::None}},
        {TEXT("field, dead"), RestTestContext(ERestTestStage::Live, ELDLifePhase::Dead), false,
            {EReason::None, EReason::None, EReason::None, NotRest, NotRest, NoReplace, NoReplace, NotRest, NotRest, EReason::None}},
        {TEXT("rest window, alive"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive), true,
            {EReason::None, EReason::None, EReason::None, EReason::None, EReason::None, EReason::None, EReason::None, EReason::None,
             EReason::None, EReason::None}},
        {TEXT("rest window, dead"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Dead), true,
            {EReason::None, EReason::None, EReason::None, EReason::Dead, EReason::Dead, EReason::Dead, EReason::Dead, EReason::Dead,
             EReason::Dead, EReason::None}},
        {TEXT("rest return step"), RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive), false,
            {EReason::None, EReason::None, EReason::None, Locked, Locked, Locked, Locked, Locked, Locked, EReason::None}},
        {TEXT("closed"), RestTestContext(ERestTestStage::Closed, ELDLifePhase::Alive), false,
            {Closed, Closed, Closed, Closed, Closed, Closed, Closed, Closed, Closed, Closed}},
        {TEXT("unspawned in the rest window"), RestTestContext(ERestTestStage::Rest, ELDLifePhase::Unspawned), true,
            {EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged,
             EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged, EReason::LifePhaseChanged,
             EReason::LifePhaseChanged, EReason::LifePhaseChanged}}};
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
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive);
    bGood &= RestTestReason(Test, TEXT("LearnGate in the rest window"),
        LDProgression::LearnGate(RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive)), EReason::None);
    bGood &= RestTestReason(Test, TEXT("LearnGate in the return step (#222)"),
        LDProgression::LearnGate(RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive)), EReason::None);
    for (const uint8 Unknown : {uint8(4), uint8(19), uint8(20), uint8(29), uint8(40), uint8(255)})
    {
        bGood &= RestTestReason(Test, FString::Printf(TEXT("Command kind %u is refused, not mapped to a neighbour"), uint32(Unknown)),
            LDProgression::CommandGate(static_cast<EKind>(Unknown), Rest), EReason::InvalidRequest);
    }

    const TPair<EKind, uint8> KindNumbers[] = {{EKind::Respec, 21}, {EKind::ReplaceActive, 22}, {EKind::ForgetActive, 23},
        {EKind::ShopBuy, 36}, {EKind::ShopRefresh, 37}, {EKind::ShopSell, 38}, {EKind::UsePickTicket, 39}};
    for (const TPair<EKind, uint8>& Row : KindNumbers)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("%s travels as %u"), RestTestKindName(Row.Key), uint32(Row.Value)),
            int32(static_cast<uint8>(Row.Key)), int32(Row.Value));
    }
    const TPair<EReason, uint16> ReasonNumbers[] = {{EReason::ReplacementNotAllowed, 48}, {EReason::BuildLocked, 70},
        {EReason::OfferOutstanding, 72}, {EReason::NothingToRespec, 73}, {EReason::SlotEmpty, 74},
        {EReason::NotRestWindow, 75}, {EReason::RestServiceUnavailable, 76}, {EReason::NotEnoughVouchers, 200},
        {EReason::ShopSoldOut, 201}, {EReason::ShopStale, 202}, {EReason::SkillPointLimit, 203}, {EReason::NoShop, 204},
        {EReason::NoPickTicket, 205}, {EReason::NotSeen, 206}, {EReason::CardInstalled, 207}};
    for (const TPair<EReason, uint16>& Row : ReasonNumbers)
    {
        const FString Name = LDGrowthWire::ReasonName(Row.Key);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as %u and decodes to its own name"), *Name, uint32(Row.Value)),
            LDGrowthWire::Raw(Row.Key) == Row.Value && Name != TEXT("Unknown")
            && Name == LDGrowthWire::ReasonName(LDGrowthWire::Reason(Row.Value)));
    }
    // 71 (the retired skill merchant's pool reason) and 77 carry no name.
    for (const uint16 Unassigned : {uint16(71), uint16(77), uint16(208)})
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Unassigned growth reason %u has no name"), uint32(Unassigned)),
            FString(LDGrowthWire::ReasonName(LDGrowthWire::Reason(Unassigned))), FString(TEXT("Unknown")));
    }
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
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive);
    FLDGrowthBuild Out;
    int64 Refunded = -1;
    // Level 3 at 108 XP earned 9 points, 4 of them allocated.
    const int64 XP = 108;
    const FLDGrowthBuild Built = RestTestBuild(Content, 3, {{Opening[0], 0}}, {}, {1, 0, 2, 0, 1});
    const int64 Before = RestTestUnspent(Content, Built, XP);
    bool bGood = Test.TestEqual(TEXT("Before the respec, earned minus allocated is unspent"), Before, int64(9 - 4));
    bGood &= RestTestReason(Test, TEXT("A respec in the rest window"),
        LDProgression::ApplyRespec(Content, Built, XP, Rest, Out, Refunded), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Every allocated point returns"),
        Refunded == 4 && Out.StatPoints == TArray<int32>({0, 0, 0, 0, 0})
        && RestTestUnspent(Content, Out, XP) == 9 && Out.Actives.Num() == 1 && Out.Level == 3 && Out.Revision == Built.Revision);
    const FLDGrowthBuild Respecced = Out;
    bGood &= RestTestReason(Test, TEXT("A respec with nothing allocated"),
        LDProgression::ApplyRespec(Content, Respecced, XP, Rest, Out, Refunded), ELDGrowthReason::NothingToRespec);
    bGood &= Test.TestTrue(TEXT("A refused respec refunds nothing"), Refunded == 0);
    bGood &= RestTestReason(Test, TEXT("A respec on the field"),
        LDProgression::ApplyRespec(Content, Built, XP, RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive), Out, Refunded),
        ELDGrowthReason::NotRestWindow);
    bGood &= Test.TestTrue(TEXT("A respec refused on the field changes nothing"), Refunded == 0 && Out.StatPoints == Built.StatPoints);
    bGood &= RestTestReason(Test, TEXT("A respec while builds are locked"),
        LDProgression::ApplyRespec(Content, Built, XP, RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive), Out,
            Refunded), ELDGrowthReason::BuildLocked);
    bGood &= RestTestReason(Test, TEXT("A respec of a build that allocated more than it earned"),
        LDProgression::ApplyRespec(Content, RestTestBuild(Content, 1, {}, {}, {5, 0, 0, 0, 0}), 0, Rest, Out, Refunded),
        ELDGrowthReason::AuthorityFault);
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
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Field = RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Locked = RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive);
    const FLDGrowthBuild Full = RestTestBuild(Content, 6, {{A, 0}, {B, 1}, {C, 2}}, {D});
    const FLDProgressionState Settled = RestTestSettled(600);
    FLDProgressionState Offered = Settled;
    Offered.ActiveEarned = 2;
    Offered.Offer.Emplace(RestTestOffer(2, {Later[0], Later[1], Later[2]}));
    FLDGrowthBuild Out;
    FName Forgotten;
    TArray<uint16> Reclaimed;
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
        Replace(Full, Settled, D, 1, RestTestContext(ERestTestStage::Live, ELDLifePhase::Dead)),
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

FLDCardInstance RestTestCard(uint16 Serial, FName Id, ELDCardTargetKind Kind = ELDCardTargetKind::None, FName Active = NAME_None)
{
    FLDCardInstance Instance;
    Instance.Serial = Serial;
    Instance.Id = Id;
    Instance.OfferOrdinal = Serial;
    Instance.Target.Kind = Kind;
    Instance.Target.Active = Active;
    return Instance;
}

// Spec §6.1 (换下即遗忘，挂件退回卡库，不复制、不丢失、不自动错绑): forgetting or replacing a skill returns every card on it to
// the library in the same commit, leaves every other install alone, binds nothing to the new skill, and a later move is the
// player's own install.
bool RestTestRunForgetReclaim(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(Test, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    using EKind = ELDCardTargetKind;
    const FName Elbow(TEXT("active_elbow")), BailOut(TEXT("active_bail_out")), Zap(TEXT("active_zap")), Seethe(TEXT("active_seethe"));
    const FName Encore(TEXT("card_encore")), Big(TEXT("card_big_one")), Practice(TEXT("card_practice"));
    const FName Spiky(TEXT("card_spiky_hide")), Breath(TEXT("card_third_breath"));
    FLDGrowthBuild Built = RestTestBuild(Content, 8, {{Elbow, 0}, {BailOut, 1}, {Zap, 2}}, {Seethe});
    Built.Cards = {RestTestCard(1, Encore, EKind::Skill, Elbow), RestTestCard(2, Encore, EKind::Skill, Elbow),
        RestTestCard(3, Big, EKind::Skill, Zap), RestTestCard(4, Practice, EKind::Skill, BailOut), RestTestCard(5, Spiky, EKind::Body),
        RestTestCard(6, Breath, EKind::Basic), RestTestCard(7, Practice)};
    Built.NextCardSerial = 8;
    LDProgression::NormalizeBuild(Built);
    if (!RestTestReason(Test, TEXT("The reclaim fixture build"), LDProgression::ValidateBuild(Content, Built), ELDGrowthReason::None))
    {
        return false;
    }
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Field = RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive);
    const FLDProgressionState Settled = RestTestSettled(2000);
    FLDGrowthBuild Out;
    FName Forgotten;
    TArray<uint16> Reclaimed;
    // The serials that left an install, in serial order, must equal Expected; everything else keeps its exact place.
    const auto Exactly = [&](const TCHAR* What, const TArray<uint16>& Expected)
    {
        bool bSame = Out.Cards.Num() == Built.Cards.Num();
        for (int32 Index = 0; bSame && Index < Built.Cards.Num(); ++Index)
        {
            const FLDCardInstance& Before = Built.Cards[Index];
            const FLDCardInstance& After = Out.Cards[Index];
            bSame = After.Serial == Before.Serial && After.Id == Before.Id && After.OfferOrdinal == Before.OfferOrdinal
                && (Expected.Contains(Before.Serial) ? !After.IsInstalled() : After.Target == Before.Target);
        }
        bool bGood = Test.TestTrue(FString::Printf(TEXT("%s: the reclaimed list"), What), Reclaimed == Expected);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s: reclaimed to the library, the rest untouched, none lost or doubled"), What), bSame);
        bGood &= RestTestReason(Test, FString::Printf(TEXT("%s: the committed build is valid"), What), LDProgression::ValidateBuild(Content, Out),
            ELDGrowthReason::None);
        return bGood;
    };

    bool bGood = RestTestReason(Test, TEXT("Forgetting Q (elbow)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 0, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting elbow"), {1, 2});
    bGood &= RestTestReason(Test, TEXT("Replacing Q (elbow) with seethe"),
        LDProgression::ApplyReplaceActive(Content, Built, Settled, Seethe, 0, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Replacing elbow"), {1, 2});
    bGood &= Test.TestTrue(TEXT("Nothing binds to the new skill on its own"), Forgotten == Elbow && Out.ActiveInSlot(0) != INDEX_NONE
        && Out.Actives[Out.ActiveInSlot(0)].Id == Seethe
        && !Out.Cards.ContainsByPredicate([&Seethe](const FLDCardInstance& Row) { return Row.Target.Active == Seethe; }));
    const FLDGrowthBuild Replaced = Out;
    FLDCardTarget OnZap;
    OnZap.Kind = EKind::Skill;
    OnZap.Active = Zap;
    FLDGrowthBuild Moved;
    bGood &= Test.TestTrue(TEXT("A reclaimed copy can move to another learned skill on the field too (#222)"),
        LDProgression::ApplyInstallCard(Content, Replaced, 1, OnZap, Field, Moved) == ELDGrowthReason::None
        && Moved.FindCard(1)->Target == OnZap && LDProgression::ValidateBuild(Content, Moved) == ELDGrowthReason::None);

    bGood &= RestTestReason(Test, TEXT("Forgetting E (zap)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 2, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting zap"), {3});
    bGood &= RestTestReason(Test, TEXT("Forgetting W (bail out)"),
        LDProgression::ApplyForgetActive(Content, Built, Settled, 1, Rest, Out, Forgotten, Reclaimed), ELDGrowthReason::None);
    bGood &= Exactly(TEXT("Forgetting bail out"), {4});

    // A build that kept a card on a lost skill is a broken record, never a quiet state.
    FLDGrowthBuild Lost = Built;
    Lost.Actives.RemoveAt(Lost.ActiveInSlot(0));
    bGood &= RestTestReason(Test, TEXT("A card left on a forgotten skill"), LDProgression::ValidateBuild(Content, Lost),
        ELDGrowthReason::DataInvalid);
    FLDGrowthBuild Misplaced = Built;
    Misplaced.FindCard(5)->Target.Kind = EKind::Dash;
    bGood &= RestTestReason(Test, TEXT("A body card on the dash"), LDProgression::ValidateBuild(Content, Misplaced),
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

// #198 §5: a respec returns the allocated points.
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

// An independent, fixed three-choice; choosing one source locks without consuming ordinary build contents.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFinalModChoiceAndLockTest, "Lockdown.Rules.DemoFinalMod.ChoiceAndLock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFinalModChoiceAndLockTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<FName> Opening, Later;
    if (!RestTestFixture(*this, Catalog, Opening, Later))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FName A = Opening[0], B = Opening[1], D = Opening[3];
    const LDProgression::FBuildContext Rest = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Locked = RestTestContext(ERestTestStage::RestLocked, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Field = RestTestContext(ERestTestStage::Live, ELDLifePhase::Alive);
    const LDProgression::FBuildContext Dead = RestTestContext(ERestTestStage::Rest, ELDLifePhase::Dead);
    const FLDGrowthBuild Build = RestTestBuild(Content, 6, {{A, 0}, {B, 1}}, {D});

    const uint64 Seed = LDProgression::FinalModSeed(FGuid(1, 2, 3, 4), FGuid(5, 6, 7, 8));
    TArray<FName> Options, Again, Other;
    bool bGood = RestTestReason(*this, TEXT("Drawing the final mod options"),
        LDProgression::DrawFinalModOptions(Content, Build, Seed, Options), ELDGrowthReason::None);
    LDProgression::DrawFinalModOptions(Content, Build, Seed, Again);
    LDProgression::DrawFinalModOptions(Content, Build, LDProgression::FinalModSeed(FGuid(1, 2, 3, 4), FGuid(9, 9, 9, 9)), Other);
    bGood &= TestTrue(TEXT("Three distinct final rules, the same for the same seed"),
        Options.Num() == 3 && Options[0] != Options[1] && Options[1] != Options[2] && Options[0] != Options[2]
        && !Options.Contains(A) && !Options.Contains(B) && !Options.Contains(D) && Options == Again);
    bGood &= TestTrue(TEXT("Another contender draws from the same pool independently"), Other.Num() == 3);
    TArray<FName> Extra, Invalid;
    bGood &= TestTrue(TEXT("An extra look keeps the original three and adds one distinct eligible option"),
        LDProgression::DrawFinalModOptions(Content, Build, Seed, Extra, 4) == ELDGrowthReason::None
        && Extra.Num() == 4 && Options.Num() == 3 && Extra[0] == Options[0] && Extra[1] == Options[1]
        && Extra[2] == Options[2] && !Options.Contains(Extra[3]));
    bGood &= TestTrue(TEXT("An unsupported option count is rejected"),
        LDProgression::DrawFinalModOptions(Content, Build, Seed, Invalid, 5) == ELDGrowthReason::DataInvalid && Invalid.IsEmpty());
    if (Options.Num() != 3)
    {
        return false;
    }
    const auto* Universal=Content.FinalMods.FindByPredicate([&Options](const FLDDemoFinalModDefinition& Row){return FName(*Row.Id)==Options[1];});
    bGood &= TestTrue(TEXT("The second option is always universal"),Universal&&Universal->IsUniversal());
    TArray<FLDFinalModTarget> MechanicTargets;
    bGood &= RestTestReason(*this,TEXT("Resolving the mechanism source"),
        LDProgression::FinalModTargets(Content,Build,Options[0],MechanicTargets),ELDGrowthReason::None);
    if(MechanicTargets.IsEmpty()){return false;}

    FLDProgressionState Open = RestTestSettled(600);
    Open.FinalMod.OfferId = FGuid(0xF1, 0, 0, 1);
    Open.FinalMod.Options = Options;
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    const FLDFinalModTarget Plain;
    const auto Take = [&](const FLDProgressionState& Progression, const FGuid& Id, FName Option, const FLDFinalModTarget& Target,
        const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyFinalMod(Content, Build, Progression, Id, Option, Target, Context, Out, OutProgression);
    };
    const auto Refused = [&](const TCHAR* What, ELDGrowthReason Actual, ELDGrowthReason Expected, const FLDProgressionState& Before)
    {
        bool bOk = RestTestReason(*this, What, Actual, Expected);
        bOk &= TestTrue(FString::Printf(TEXT("%s changes nothing"), What), Out.Actives.Num() == Build.Actives.Num()
            && Out.FinalMod==Build.FinalMod && Out.PendingBlueprints==Build.PendingBlueprints
            && OutProgression.FinalMod.OfferId == Before.FinalMod.OfferId
            && OutProgression.FinalMod.Options == Before.FinalMod.Options && OutProgression.FinalMod.bLocked == Before.FinalMod.bLocked
            && Out.Cards.Num()==Build.Cards.Num());
        return bOk;
    };

    bGood &= RestTestReason(*this, TEXT("Taking an independent universal rule"), Take(Open, Open.FinalMod.OfferId, Options[1], Plain, Rest),
        ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("Ordinary actives, blueprints, cards and the empty E slot are preserved"),
        Out.Actives.Num()==Build.Actives.Num() && Out.FindActive(A) && Out.FindActive(B) && Out.ActiveInSlot(2)==INDEX_NONE
        && Out.PendingBlueprints==Build.PendingBlueprints && Out.Cards.Num()==Build.Cards.Num()
        && Out.FinalMod.DefinitionId==Options[1] && Out.FinalMod.TargetKind==ELDFinalModTargetKind::None
        && OutProgression.FinalMod.bLocked
        && !OutProgression.FinalMod.IsPending() && LDProgression::ValidateBuild(Content, Out) == ELDGrowthReason::None);
    const FLDProgressionState Taken = OutProgression;

    bGood &= RestTestReason(*this, TEXT("Taking a mechanism with an explicit legal source"),
        Take(Open, Open.FinalMod.OfferId, Options[0], MechanicTargets[0], Rest),
        ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The source is saved without replacing Q"),
        Out.FindActive(A) && Out.FindActive(B) && Out.Actives.Num()==Build.Actives.Num()
        && Out.FinalMod.TargetKind==MechanicTargets[0].Kind && Out.FinalMod.TargetId==MechanicTargets[0].Definition
        && OutProgression.FinalMod.bLocked);

    // ---- Every other attempt is refused with nothing changed ----
    bGood &= Refused(TEXT("Taking it again once locked"), Take(Taken, Open.FinalMod.OfferId, Options[1], Plain, Rest), ELDGrowthReason::FinalLocked, Taken);
    bGood &= Refused(TEXT("Taking it while builds are locked"), Take(Open, Open.FinalMod.OfferId, Options[1], Plain, Locked),
        ELDGrowthReason::BuildLocked, Open);
    bGood &= Refused(TEXT("Taking it on the field"), Take(Open, Open.FinalMod.OfferId, Options[1], Plain, Field), ELDGrowthReason::NotRestWindow, Open);
    bGood &= Refused(TEXT("Taking it while dead"), Take(Open, Open.FinalMod.OfferId, Options[1], Plain, Dead), ELDGrowthReason::Dead, Open);
    bGood &= Refused(TEXT("Taking another final mod id"), Take(Open, FGuid(0xF1, 0, 0, 2), Options[1], Plain, Rest), ELDGrowthReason::NoFinalMod, Open);
    const FLDProgressionState BeforeOpen = RestTestSettled(600);
    bGood &= Refused(TEXT("Taking it before it opened"), Take(BeforeOpen, Open.FinalMod.OfferId, Options[1], Plain, Rest),
        ELDGrowthReason::NoFinalMod, BeforeOpen);
    bGood &= Refused(TEXT("Taking an active outside the final options"), Take(Open, Open.FinalMod.OfferId, D, Plain, Rest), ELDGrowthReason::InvalidOption, Open);
    FLDFinalModTarget Wrong;
    Wrong.Kind=ELDFinalModTargetKind::Active;Wrong.Definition=A;
    bGood &= Refused(TEXT("Binding a universal rule to an explicit active"),Take(Open,Open.FinalMod.OfferId,Options[1],Wrong,Rest),
        ELDGrowthReason::FinalModUnavailable,Open);
    FLDProgressionState Offered = Open;
    Offered.ActiveEarned = 2;
    Offered.Offer.Emplace(RestTestOffer(2, {Later[0], Later[1], Later[2]}));
    bGood &= RestTestReason(*this,TEXT("Independent choice may lock while an ordinary offer is outstanding"),
        Take(Offered,Open.FinalMod.OfferId,Options[1],Plain,Rest),ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The ordinary offer is preserved, not consumed as a final card"),OutProgression.Offer.IsSet()
        && OutProgression.Offer->OfferId==Offered.Offer->OfferId && Out.Actives.Num()==Build.Actives.Num());
    bGood &= TestEqual(TEXT("The final mod gate refuses a locked rest"),
        LDGrowthWire::ReasonName(LDProgression::CommandGate(ELDBuildCommandKind::FinalMod, Locked)),
        LDGrowthWire::ReasonName(ELDGrowthReason::BuildLocked));

    // ---- The build stays adjustable: a taken card whose source left is dormant and rebinds anywhere an install is ----
    FLDGrowthBuild Dormant = Build;
    Dormant.FinalMod.DefinitionId = Options[0];
    Dormant.FinalMod.TargetKind = ELDFinalModTargetKind::Active;
    Dormant.FinalMod.TargetId = FName(TEXT("removed_source"));
    bGood &= TestTrue(TEXT("A card whose source was removed is dormant, not invalid"),
        !LDProgression::FinalModBound(Content, Dormant) && LDProgression::ValidateBuild(Content, Dormant) == ELDGrowthReason::None);
    bGood &= RestTestReason(*this, TEXT("Rebinding a dormant card on the field"), LDProgression::ApplyFinalMod(Content, Dormant, Taken,
        Open.FinalMod.OfferId, Options[0], MechanicTargets[0], Field, Out, OutProgression), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The rebind only moves the binding"), Out.FinalMod.DefinitionId == Options[0]
        && Out.FinalMod.TargetKind == MechanicTargets[0].Kind && Out.FinalMod.TargetId == MechanicTargets[0].Definition
        && LDProgression::FinalModBound(Content, Out) && OutProgression.FinalMod.bLocked && Out.Actives.Num() == Build.Actives.Num());
    const FLDGrowthBuild Rebound = Out;
    bGood &= RestTestReason(*this, TEXT("A bound card does not rebind"), LDProgression::ApplyFinalMod(Content, Rebound, Taken,
        Open.FinalMod.OfferId, Options[0], MechanicTargets.Last(), Rest, Out, OutProgression), ELDGrowthReason::FinalLocked);
    bGood &= RestTestReason(*this, TEXT("A dormant card does not swap for another option"), LDProgression::ApplyFinalMod(Content, Dormant,
        Taken, Open.FinalMod.OfferId, Options[1], Plain, Field, Out, OutProgression), ELDGrowthReason::NotRestWindow);
    return bGood;
}

#endif
