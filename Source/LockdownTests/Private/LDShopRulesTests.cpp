#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/AllOf.h"
#include "Algo/Compare.h"
#include "Combat/LDChainRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"

// Issue #292 P5 (spec §3.13, §6.4, §6.5): the rest shop paid in stat points and the offer skips as pure LDProgression rules,
// and the chain key rules (LDChain).
namespace LDShopRulesTests
{
using EStage = LDProgression::EMatchStage;

LDProgression::FBuildContext Context(EStage Stage, ELDLifePhase Phase = ELDLifePhase::Alive)
{
    LDProgression::FBuildContext Out;
    Out.Stage = Stage;
    Out.LifePhase = Phase;
    return Out;
}

bool Reason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

const FGuid Secret(0x292, 1, 2, 3);
const FGuid Contender(0x292, 4, 5, 6);

// A level-6 build with the elbow on Q (the shop's skill slots draw other actives) and Unspent of its earned stat points left
// (the rest allocated to the first stat).
FLDGrowthBuild Build(const FLDDemoContentDefinitions& Content, int64 Unspent = 18)
{
    FLDGrowthBuild Out = LDProgression::StartBuild(Content);
    Out.Level = 6;
    Out.Actives.Add({FName(TEXT("active_elbow")), 0});
    int64 Earned = 0;
    LDProgression::EarnedPoints(Content, 6, Content.Progression.XPThresholds[5], Earned);
    Out.StatPoints[0] = int32(Earned - Unspent);
    LDProgression::NormalizeBuild(Out);
    return Out;
}

int64 Unspent(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Of, const FLDProgressionState& State)
{
    int64 Out = -1;
    LDProgression::UnspentPoints(Content, Of, State.CumulativeXP, Out);
    return Out;
}

// A stocked shop of a level-6 contender.
bool Stocked(FAutomationTestBase& Test, const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Of, FLDProgressionState& Out)
{
    Out = FLDProgressionState();
    Out.CumulativeXP = Content.Progression.XPThresholds[5];
    return Reason(Test, TEXT("The shop stocks"), LDProgression::StockShop(Content, Of, Out, Secret, Contender, true), ELDGrowthReason::None);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthShopStockTest, "Lockdown.Rules.Growth.Shop.Stock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthShopStockTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoShopParameters& Shop = Content.Parameters.Shop;
    const FLDGrowthBuild Of = Build(Content);
    FLDProgressionState Progression;
    if (!Stocked(*this, Content, Of, Progression)) { return false; }
    const TArray<FLDShopSlot>& Slots = Progression.Shop.Slots;
    TSet<FName> Skills, Cards;
    for (const FLDShopSlot& Slot : Slots)
    {
        (Slot.Kind == ELDShopItemKind::Skill ? Skills : Cards).Add(Slot.Definition);
    }
    bool bGood = TestTrue(TEXT("Spec §3.13: two skill slots then four card slots, all distinct and unsold"),
        Slots.Num() == Shop.SkillSlots + Shop.CardSlots && Skills.Num() == Shop.SkillSlots && Cards.Num() == Shop.CardSlots
        && Slots[0].Kind == ELDShopItemKind::Skill && Slots.Last().Kind == ELDShopItemKind::Card
        && !Slots.ContainsByPredicate([](const FLDShopSlot& Slot) { return Slot.bSold; }));
    bGood &= TestFalse(TEXT("A skill slot never holds a learned skill"), Skills.Contains(FName(TEXT("active_elbow"))));
    bGood &= TestTrue(TEXT("The stock is a stocked revision with no refresh yet"),
        Progression.Shop.bStocked && Progression.Shop.Revision == 1 && Progression.Shop.Refreshes == 0 && Progression.Shop.Stocks == 1);
    FLDProgressionState Again;
    Stocked(*this, Content, Of, Again);
    bGood &= TestTrue(TEXT("The same seed draws the same shelf"), Algo::Compare(Again.Shop.Slots, Slots,
        [](const FLDShopSlot& A, const FLDShopSlot& B) { return A.Kind == B.Kind && A.Definition == B.Definition; }));
    bGood &= TestEqual(TEXT("Prices: card, skill, skill point"), FString::Printf(TEXT("%d/%d/%d"),
        LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Card, Progression.Shop),
        LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Skill, Progression.Shop),
        LDProgression::ShopSkillPointPrice(Content.Parameters, Progression.Shop)), FString::Printf(TEXT("%d/%d/%d"), 2, 2, 3));
    {
        // Spec §11.1 sale day: every price halves, rounded up; the free first refresh stays free.
        FLDShopState Sale = Progression.Shop;
        Sale.bSale = true;
        bGood &= TestEqual(TEXT("Sale-day prices: card, skill, skill point, first refresh"), FString::Printf(TEXT("%d/%d/%d/%d"),
            LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Card, Sale),
            LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Skill, Sale),
            LDProgression::ShopSkillPointPrice(Content.Parameters, Sale), LDProgression::ShopRefreshPrice(Content.Parameters, Sale)),
            FString::Printf(TEXT("%d/%d/%d/%d"), 1, 1, 2, 0));
    }
    // The vocabulary weighting favours cards sharing the build's keys in the weighted slots (statistically over many seeds).
    int32 Weighted = 0, Plain = 0;
    const uint32 Vocab = LDProgression::BuildVocab(Content, Of);
    for (uint32 Seed = 1; Seed <= 200; ++Seed)
    {
        TArray<FLDShopSlot> Draw;
        LDProgression::DrawShop(Content, Of, LDProgression::ShopSeed(Secret, Contender, Seed), Draw);
        for (int32 Index = Shop.SkillSlots; Index < Draw.Num(); ++Index)
        {
            const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate([&](const FLDDemoCardDefinition& Row)
            { return FName(*Row.Id) == Draw[Index].Definition; });
            const bool bShares = Card && (Card->VocabMask & Vocab) != 0;
            (Index - Shop.SkillSlots < Shop.VocabCardSlots ? Weighted : Plain) += bShares ? 1 : 0;
        }
    }
    bGood &= TestTrue(TEXT("The weighted slots show more vocabulary cards than the uniform ones"), Weighted > Plain);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthShopBuyTest, "Lockdown.Rules.Growth.Shop.Buy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthShopBuyTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoShopParameters& Terms = Content.Parameters.Shop;
    const FLDGrowthBuild Of = Build(Content);
    const LDProgression::FBuildContext Rest = Context(EStage::Rest);
    FLDProgressionState Rich;
    if (!Stocked(*this, Content, Of, Rich)) { return false; }
    const int64 Points = Unspent(Content, Of, Rich);
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    LDProgression::FShopOutcome Outcome;
    const uint8 CardSlot = uint8(Terms.SkillSlots);
    bool bGood = Reason(*this, TEXT("Buying a card"), LDProgression::ApplyShopBuy(Content, Of, Rich, CardSlot, Rich.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The card joins the library for its stat points and the slot is sold"),
        Outcome.Kind == ELDShopItemKind::Card && Out.Cards.Num() == 1 && !Out.Cards[0].IsInstalled() && Out.Cards[0].Id == Rich.Shop.Slots[CardSlot].Definition
        && Unspent(Content, Out, OutProgression) == Points - Terms.CardPrice && Out.ShopSpentPoints == Terms.CardPrice
        && OutProgression.Shop.Slots[CardSlot].bSold && OutProgression.Shop.Revision == Rich.Shop.Revision + 1 && Outcome.PointsPaid == Terms.CardPrice);
    {
        // Spec §3.13: a respec never returns what the shop took.
        FLDGrowthBuild Respecced;
        int64 Refunded = 0;
        bGood &= Reason(*this, TEXT("A respec after a purchase"), LDProgression::ApplyRespec(Content, Out, Rich.CumulativeXP, {}, Rest, Respecced,
            Refunded), ELDGrowthReason::None);
        bGood &= TestTrue(TEXT("It returns the allocated points only"), Respecced.ShopSpentPoints == Terms.CardPrice
            && Unspent(Content, Respecced, OutProgression) == 18 - Terms.CardPrice);
    }
    const FLDProgressionState Bought = OutProgression;
    const FLDGrowthBuild BoughtBuild = Out;
    bGood &= Reason(*this, TEXT("A sold slot"), LDProgression::ApplyShopBuy(Content, BoughtBuild, Bought, CardSlot, Bought.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::ShopSoldOut);
    bGood &= Reason(*this, TEXT("An older shelf revision"), LDProgression::ApplyShopBuy(Content, BoughtBuild, Bought, CardSlot + 1, Rich.Shop.Revision,
        Rest, Out, OutProgression, Outcome), ELDGrowthReason::ShopStale);
    // A skill learns into the lowest empty slot (W here).
    bGood &= Reason(*this, TEXT("Buying a skill"), LDProgression::ApplyShopBuy(Content, Of, Rich, 0, Rich.Shop.Revision, Rest, Out, OutProgression,
        Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The skill is learned into the lowest empty slot for its price"), Outcome.Kind == ELDShopItemKind::Skill
        && Outcome.Choice.Result == ELDBuildChoiceResult::Learned && Outcome.Choice.Slot == 1 && Out.FindActive(Rich.Shop.Slots[0].Definition)
        && Unspent(Content, Out, OutProgression) == Points - Terms.SkillPrice);
    // Full Q/W/E: it waits as a pending blueprint.
    FLDGrowthBuild Full = Of;
    Full.Actives.Add({FName(TEXT("active_pants_down")), 1});
    Full.Actives.Add({FName(TEXT("active_crotch_grab")), 2});
    LDProgression::NormalizeBuild(Full);
    FLDProgressionState FullShop;
    Stocked(*this, Content, Full, FullShop);
    bGood &= Reason(*this, TEXT("Buying a skill with full slots"), LDProgression::ApplyShopBuy(Content, Full, FullShop, 0, FullShop.Shop.Revision, Rest,
        Out, OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("It is kept as a blueprint"), Outcome.Choice.Result == ELDBuildChoiceResult::StoredAsBlueprint
        && Out.HasBlueprint(FullShop.Shop.Slots[0].Definition));
    // The skill point: at most SHOP.SkillPointMaxPerMatch per match.
    const uint8 Point = FLDBuildCommand::ShopSkillPointSlot;
    const int32 Before = LDProgression::SkillPoints(Content, Of);
    bGood &= Reason(*this, TEXT("Buying a skill point"), LDProgression::ApplyShopBuy(Content, Of, Rich, Point, Rich.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The bought point is a skill point for its price"), Out.BoughtSkillPoints == 1
        && LDProgression::SkillPoints(Content, Out) == Before + 1 && Unspent(Content, Out, OutProgression) == Points - Terms.SkillPointPrice);
    FLDGrowthBuild Maxed = Of;
    Maxed.BoughtSkillPoints = Terms.SkillPointMaxPerMatch;
    bGood &= Reason(*this, TEXT("Past the match's skill points"), LDProgression::ApplyShopBuy(Content, Maxed, Rich, Point, Rich.Shop.Revision, Rest,
        Out, OutProgression, Outcome), ELDGrowthReason::SkillPointLimit);
    const FLDGrowthBuild Poor = Build(Content, Terms.CardPrice - 1);
    bGood &= Reason(*this, TEXT("Short of the price"), LDProgression::ApplyShopBuy(Content, Poor, Rich, CardSlot, Rich.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::NotEnoughPoints);
    bGood &= TestEqual(TEXT("A refused purchase spends nothing"), Out.ShopSpentPoints, int64(0));
    bGood &= Reason(*this, TEXT("No shop before the first stock (the opening rest)"), LDProgression::ApplyShopBuy(Content, Of, FLDProgressionState(),
        CardSlot, 1, Rest, Out, OutProgression, Outcome), ELDGrowthReason::NoShop);
    bGood &= Reason(*this, TEXT("The shop on the field"), LDProgression::ApplyShopBuy(Content, Of, Rich, CardSlot, Rich.Shop.Revision,
        Context(EStage::Live), Out, OutProgression, Outcome), ELDGrowthReason::NotRestWindow);
    bGood &= Reason(*this, TEXT("The shop in a locked rest step"), LDProgression::ApplyShopBuy(Content, Of, Rich, CardSlot, Rich.Shop.Revision,
        Context(EStage::RestLocked), Out, OutProgression, Outcome), ELDGrowthReason::BuildLocked);
    bGood &= Reason(*this, TEXT("The shop while dead"), LDProgression::ApplyShopBuy(Content, Of, Rich, CardSlot, Rich.Shop.Revision,
        Context(EStage::Rest, ELDLifePhase::Dead), Out, OutProgression, Outcome), ELDGrowthReason::Dead);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthShopRefreshTest, "Lockdown.Rules.Growth.Shop.Refresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthShopRefreshTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoShopParameters& Terms = Content.Parameters.Shop;
    const FLDGrowthBuild Of = Build(Content);
    const LDProgression::FBuildContext Rest = Context(EStage::Rest);
    FLDProgressionState State;
    if (!Stocked(*this, Content, Of, State)) { return false; }
    FLDGrowthBuild Paying = Of;
    bool bGood = true;
    int32 Spent = 0;
    // Spec §3.13: free, then 1, 2 ... within one rest.
    for (int32 Refresh = 0; Refresh < 3; ++Refresh)
    {
        const int32 Price = Terms.RefreshPrice + Terms.RefreshStep * Refresh;
        bGood &= TestEqual(FString::Printf(TEXT("Refresh %d costs %d"), Refresh + 1, Price), LDProgression::ShopRefreshPrice(Content.Parameters, State.Shop),
            Price);
        FLDProgressionState Next;
        FLDGrowthBuild NextBuild;
        int32 Paid = 0;
        bGood &= Reason(*this, TEXT("A refresh"), LDProgression::ApplyShopRefresh(Content, Paying, State, State.Shop.Revision, Secret, Contender,
            Rest, NextBuild, Next, Paid), ELDGrowthReason::None);
        bGood &= TestTrue(TEXT("It pays its price and draws a new stock"), Paid == Price
            && NextBuild.ShopSpentPoints == Paying.ShopSpentPoints + Price && Next.Shop.Stocks == State.Shop.Stocks + 1
            && Next.Shop.Refreshes == Refresh + 1);
        Spent += Paid;
        State = Next;
        Paying = NextBuild;
    }
    bGood &= TestEqual(TEXT("Three refreshes cost 0 + 1 + 2"), Spent, 3 * Terms.RefreshPrice + 3 * Terms.RefreshStep);
    FLDProgressionState Stale;
    FLDGrowthBuild StaleBuild;
    int32 Paid = 0;
    bGood &= Reason(*this, TEXT("A refresh of an older shelf"), LDProgression::ApplyShopRefresh(Content, Paying, State, State.Shop.Revision - 1,
        Secret, Contender, Rest, StaleBuild, Stale, Paid), ELDGrowthReason::ShopStale);
    bGood &= Reason(*this, TEXT("A paid refresh without the stat points"), LDProgression::ApplyShopRefresh(Content, Build(Content, 0), State,
        State.Shop.Revision, Secret, Contender, Rest, StaleBuild, Stale, Paid), ELDGrowthReason::NotEnoughPoints);
    // The next rest start restocks and resets the price.
    bGood &= Reason(*this, TEXT("The next rest restocks"), LDProgression::StockShop(Content, Of, State, Secret, Contender, true), ELDGrowthReason::None);
    bGood &= TestEqual(TEXT("A new rest starts at the base refresh price"), LDProgression::ShopRefreshPrice(Content.Parameters, State.Shop),
        Terms.RefreshPrice);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthShopSkipPointsTest, "Lockdown.Rules.Growth.Shop.SkipPoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthShopSkipPointsTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    // Spec §6.4: skipping a card offer pays CARDS.SkipPoints, skipping a skill offer GROWTH.ActiveSkipPoints, as awarded points.
    const FLDGrowthBuild Of = Build(Content, 0);
    const LDProgression::FBuildContext Rest = Context(EStage::Rest);
    FLDProgressionState Cards;
    Cards.CumulativeXP = Content.Progression.XPThresholds[5];
    Cards.ActiveEarned = 1;
    Cards.ActiveConsumed = 1;
    Cards.Records.Add({1, FGuid(1, 1, 1, 1), NAME_None, 1});
    uint16 First = 0, Last = 0;
    LDProgression::QueueCardOffers(Cards, ELDCardOfferSource::LevelUp, 1, First, Last);
    bool bDrawn = false;
    LDProgression::RefreshCardOffer(Content, Of, Cards, Secret, Contender, bDrawn);
    FLDProgressionState Skipped;
    FLDGrowthBuild SkippedBuild;
    bool bGood = Reason(*this, TEXT("Skipping a card offer"), LDProgression::ApplySkipCards(Content, Of, Cards, Cards.CardOffer->OfferId, Rest,
        SkippedBuild, Skipped), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("It pays CARDS.SkipPoints (1) and resolves the offer"), SkippedBuild.AwardedPoints == 1
        && Unspent(Content, SkippedBuild, Skipped) == 1 && !Skipped.CardOffer.IsSet());
    FLDGrowthBuild Active;
    int64 Paid = 0;
    bGood &= Reason(*this, TEXT("Skipping a skill offer"), LDProgression::ApplySkipActiveOffer(Content, Of, Rest, Active, Paid),
        ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("It pays GROWTH.ActiveSkipPoints (1)"), Paid == 1 && Active.AwardedPoints == 1 && Unspent(Content, Active, Cards) == 1);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatChainKeyTest, "Lockdown.Rules.Combat.Chain.Key",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCombatChainKeyTest::RunTest(const FString& Parameters)
{
    // Spec §6.5: each kind once, in the order it first appeared; three kinds count.
    const FName Encore(TEXT("card_encore")), Overload = LDChain::ReactionStep(TEXT("Overload")), Kill = LDChain::KillStep();
    TArray<FName> Key;
    LDChain::Key({Encore, Overload, Encore, Kill, Overload}, Key);
    bool bGood = TestTrue(TEXT("The key keeps first appearances"), Key == TArray<FName>({Encore, Overload, Kill}));
    bGood &= TestEqual(TEXT("The log key"), LDChain::Text(Key, TEXT(">")), FString(TEXT("card_encore>reaction_overload>kill")));
    bGood &= TestTrue(TEXT("Three kinds count; two do not"), LDChain::Counts(Key, 3) && !LDChain::Counts({Encore, Kill}, 3));
    bGood &= TestEqual(TEXT("A ground step"), LDChain::GroundStep(TEXT("Ice")), FName(TEXT("ground_ice")));
    TArray<int32> Longest;
    for (const int32 Steps : {3, 7, 4, 9, 5, 6, 2})
    {
        LDChain::KeepLongest(Longest, Steps, 5);
    }
    bGood &= TestTrue(TEXT("Only the five longest stay, longest first"), Longest == TArray<int32>({9, 7, 6, 5, 4}));
    bGood &= TestFalse(TEXT("A shorter chain does not enter a full list"), LDChain::KeepLongest(Longest, 3, 5));
    return bGood;
}

#endif
