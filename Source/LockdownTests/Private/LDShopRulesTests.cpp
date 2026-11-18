#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/AllOf.h"
#include "Algo/Compare.h"
#include "Combat/LDChainRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"

// Issue #292 P5 (spec §3.13, §6.4, §6.5, §11.3): vouchers, the rest shop and pick tickets as pure LDProgression rules, and the
// chain key rules (LDChain).
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

// A level-6 build with the elbow on Q (the shop's skill slots draw other actives).
FLDGrowthBuild Build(const FLDDemoContentDefinitions& Content)
{
    FLDGrowthBuild Out = LDProgression::StartBuild(Content);
    Out.Level = 6;
    Out.Actives.Add({FName(TEXT("active_elbow")), 0});
    LDProgression::NormalizeBuild(Out);
    return Out;
}

// A stocked shop with Vouchers vouchers.
bool Stocked(FAutomationTestBase& Test, const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Of, int32 Vouchers,
    FLDProgressionState& Out)
{
    Out = FLDProgressionState();
    Out.Vouchers = Vouchers;
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
    if (!Stocked(*this, Content, Of, 0, Progression)) { return false; }
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
    bGood &= TestTrue(TEXT("Every shelved card is seen this match (a pick ticket may name it)"),
        Algo::AllOf(Cards, [&Progression](FName Card) { return Progression.SeenCards.Contains(Card); }));
    bGood &= TestTrue(TEXT("The stock is a stocked revision with no refresh yet"),
        Progression.Shop.bStocked && Progression.Shop.Revision == 1 && Progression.Shop.Refreshes == 0 && Progression.Shop.Stocks == 1);
    FLDProgressionState Again;
    Stocked(*this, Content, Of, 0, Again);
    bGood &= TestTrue(TEXT("The same seed draws the same shelf"), Algo::Compare(Again.Shop.Slots, Slots,
        [](const FLDShopSlot& A, const FLDShopSlot& B) { return A.Kind == B.Kind && A.Definition == B.Definition; }));
    bGood &= TestEqual(TEXT("Prices: card, skill, skill point"), FString::Printf(TEXT("%d/%d/%d"),
        LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Card), LDProgression::ShopPrice(Content.Parameters, ELDShopItemKind::Skill),
        Shop.SkillPointPrice), FString::Printf(TEXT("%d/%d/%d"), 6, 10, 25));
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
    if (!Stocked(*this, Content, Of, 100, Rich)) { return false; }
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    LDProgression::FShopOutcome Outcome;
    const uint8 CardSlot = uint8(Terms.SkillSlots);
    bool bGood = Reason(*this, TEXT("Buying a card"), LDProgression::ApplyShopBuy(Content, Of, Rich, CardSlot, Rich.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The card joins the library for its price and the slot is sold"),
        Outcome.Kind == ELDShopItemKind::Card && Out.Cards.Num() == 1 && !Out.Cards[0].IsInstalled() && Out.Cards[0].Id == Rich.Shop.Slots[CardSlot].Definition
        && OutProgression.Vouchers == 100 - Terms.CardPrice && OutProgression.Shop.Slots[CardSlot].bSold
        && OutProgression.Shop.Revision == Rich.Shop.Revision + 1 && Outcome.VouchersPaid == Terms.CardPrice);
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
        && OutProgression.Vouchers == 100 - Terms.SkillPrice);
    // Full Q/W/E: it waits as a pending blueprint.
    FLDGrowthBuild Full = Of;
    Full.Actives.Add({FName(TEXT("active_pants_down")), 1});
    Full.Actives.Add({FName(TEXT("active_crotch_grab")), 2});
    LDProgression::NormalizeBuild(Full);
    FLDProgressionState FullShop;
    Stocked(*this, Content, Full, 100, FullShop);
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
        && LDProgression::SkillPoints(Content, Out) == Before + 1 && OutProgression.Vouchers == 100 - Terms.SkillPointPrice);
    FLDGrowthBuild Maxed = Of;
    Maxed.BoughtSkillPoints = Terms.SkillPointMaxPerMatch;
    bGood &= Reason(*this, TEXT("Past the match's skill points"), LDProgression::ApplyShopBuy(Content, Maxed, Rich, Point, Rich.Shop.Revision, Rest,
        Out, OutProgression, Outcome), ELDGrowthReason::SkillPointLimit);
    FLDProgressionState Poor = Rich;
    Poor.Vouchers = Terms.CardPrice - 1;
    bGood &= Reason(*this, TEXT("Short of the price"), LDProgression::ApplyShopBuy(Content, Of, Poor, CardSlot, Poor.Shop.Revision, Rest, Out,
        OutProgression, Outcome), ELDGrowthReason::NotEnoughVouchers);
    bGood &= TestEqual(TEXT("A refused purchase spends nothing"), OutProgression.Vouchers, Poor.Vouchers);
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
    if (!Stocked(*this, Content, Of, 100, State)) { return false; }
    bool bGood = true;
    int32 Spent = 0;
    // Spec §3.13: 2, 3, 4 ... within one rest.
    for (int32 Refresh = 0; Refresh < 3; ++Refresh)
    {
        const int32 Price = Terms.RefreshPrice + Terms.RefreshStep * Refresh;
        bGood &= TestEqual(FString::Printf(TEXT("Refresh %d costs %d"), Refresh + 1, Price), LDProgression::ShopRefreshPrice(Content.Parameters, State.Shop),
            Price);
        FLDProgressionState Next;
        int32 Paid = 0;
        bGood &= Reason(*this, TEXT("A refresh"), LDProgression::ApplyShopRefresh(Content, Of, State, State.Shop.Revision, Secret, Contender, Rest,
            Next, Paid), ELDGrowthReason::None);
        bGood &= TestTrue(TEXT("It pays its price and draws a new stock"), Paid == Price && Next.Vouchers == State.Vouchers - Price
            && Next.Shop.Stocks == State.Shop.Stocks + 1 && Next.Shop.Refreshes == Refresh + 1);
        Spent += Paid;
        State = Next;
    }
    bGood &= TestEqual(TEXT("Three refreshes cost 2 + 3 + 4"), Spent, 3 * Terms.RefreshPrice + 3 * Terms.RefreshStep);
    FLDProgressionState Stale;
    int32 Paid = 0;
    bGood &= Reason(*this, TEXT("A refresh of an older shelf"), LDProgression::ApplyShopRefresh(Content, Of, State, State.Shop.Revision - 1, Secret,
        Contender, Rest, Stale, Paid), ELDGrowthReason::ShopStale);
    FLDProgressionState Poor = State;
    Poor.Vouchers = 0;
    bGood &= Reason(*this, TEXT("A refresh without the vouchers"), LDProgression::ApplyShopRefresh(Content, Of, Poor, Poor.Shop.Revision, Secret,
        Contender, Rest, Stale, Paid), ELDGrowthReason::NotEnoughVouchers);
    // The next rest start restocks and resets the price.
    bGood &= Reason(*this, TEXT("The next rest restocks"), LDProgression::StockShop(Content, Of, State, Secret, Contender, true), ELDGrowthReason::None);
    bGood &= TestEqual(TEXT("A new rest starts at the base refresh price"), LDProgression::ShopRefreshPrice(Content.Parameters, State.Shop),
        Terms.RefreshPrice);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthShopSellTest, "Lockdown.Rules.Growth.Shop.Sell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthShopSellTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    FLDGrowthBuild Of = Build(Content);
    uint16 Library = 0, Installed = 0;
    LDProgression::ReceiveCard(Content, Of, FName(TEXT("card_spiky_hide")), 0, Library);
    LDProgression::ReceiveCard(Content, Of, FName(TEXT("card_thick_hide")), 0, Installed);
    Of.FindCard(Installed)->Target.Kind = ELDCardTargetKind::Body;
    const LDProgression::FBuildContext Rest = Context(EStage::Rest);
    FLDProgressionState State;
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    int32 Paid = 0;
    bool bGood = Reason(*this, TEXT("Selling a library card"), LDProgression::ApplyShopSell(Content, Of, State, Library, Rest, Out, OutProgression, Paid),
        ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("It leaves the build for SHOP.SellPrice"), Paid == Content.Parameters.Shop.SellPrice && !Out.FindCard(Library)
        && Out.FindCard(Installed) && OutProgression.Vouchers == Paid);
    bGood &= Reason(*this, TEXT("Selling an installed card"), LDProgression::ApplyShopSell(Content, Of, State, Installed, Rest, Out, OutProgression,
        Paid), ELDGrowthReason::CardInstalled);
    bGood &= Reason(*this, TEXT("Selling a card not owned"), LDProgression::ApplyShopSell(Content, Of, State, 999, Rest, Out, OutProgression, Paid),
        ELDGrowthReason::NotOwned);
    bGood &= Reason(*this, TEXT("Selling on the field"), LDProgression::ApplyShopSell(Content, Of, State, Library, Context(EStage::Live), Out,
        OutProgression, Paid), ELDGrowthReason::NotRestWindow);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthVouchersSourcesTest, "Lockdown.Rules.Growth.Vouchers.Sources",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthVouchersSourcesTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoContentParameters& P = Content.Parameters;
    // Spec §3.13 amounts (the P6 sources, the bounty, lockers and the mid-field objectives, are not wired yet).
    bool bGood = TestEqual(TEXT("Camp clears pay 2 / 4 / 8 / 12 by tier"), FString::Printf(TEXT("%d/%d/%d/%d"),
        LDProgression::CampClearVouchers(P, ELDDemoCampTier::Low), LDProgression::CampClearVouchers(P, ELDDemoCampTier::High),
        LDProgression::CampClearVouchers(P, ELDDemoCampTier::Elite), LDProgression::CampClearVouchers(P, ELDDemoCampTier::Ancient)),
        FString(TEXT("2/4/8/12")));
    bGood &= TestEqual(TEXT("The Boss, a kill, an assist, the airdrop"), FString::Printf(TEXT("%d/%d/%d/%d"), P.Vouchers.Boss, P.Vouchers.Kill,
        P.Vouchers.Assist, P.Vouchers.Airdrop), FString(TEXT("20/10/4/6")));
    bGood &= TestEqual(TEXT("A Boss camp pays no camp clear"), LDProgression::CampClearVouchers(P, ELDDemoCampTier::Boss), 0);
    // Skipping a card offer pays CARDS.SkipVouchers; skipping a skill offer pays VOUCHER.ActiveSkip (no attribute points).
    const FLDGrowthBuild Of = Build(Content);
    const LDProgression::FBuildContext Rest = Context(EStage::Rest);
    FLDProgressionState Cards;
    Cards.ActiveEarned = 1;
    Cards.ActiveConsumed = 1;
    Cards.Records.Add({1, FGuid(1, 1, 1, 1), NAME_None, 1});
    uint16 First = 0, Last = 0;
    LDProgression::QueueCardOffers(Cards, ELDCardOfferSource::LevelUp, 1, First, Last);
    bool bDrawn = false;
    LDProgression::RefreshCardOffer(Content, Of, Cards, Secret, Contender, bDrawn);
    FLDProgressionState Skipped;
    bGood &= Reason(*this, TEXT("Skipping a card offer"), LDProgression::ApplySkipCards(Content, Cards, Cards.CardOffer->OfferId, Rest, Skipped),
        ELDGrowthReason::None);
    bGood &= TestEqual(TEXT("It pays CARDS.SkipVouchers (3)"), Skipped.Vouchers, 3);
    FLDProgressionState Active;
    int32 Paid = 0;
    bGood &= Reason(*this, TEXT("Skipping a skill offer"), LDProgression::ApplySkipActiveOffer(Content, Of, Cards, Rest, Active, Paid),
        ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("It pays VOUCHER.ActiveSkip (8) and no point"), Paid == 8 && Active.Vouchers == 8);
    FLDProgressionState Full;
    Full.Vouchers = MAX_int32;
    bGood &= Reason(*this, TEXT("A grant past int32"), LDProgression::GrantVouchers(Full, 1), ELDGrowthReason::ArithmeticOverflow);
    bGood &= Reason(*this, TEXT("A negative grant"), LDProgression::GrantVouchers(Full, -1), ELDGrowthReason::DataInvalid);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthPickTicketTest, "Lockdown.Rules.Growth.PickTicket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDGrowthPickTicketTest::RunTest(const FString& Parameters)
{
    using namespace LDShopRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDGrowthBuild Of = Build(Content);
    const LDProgression::FBuildContext Field = Context(EStage::Live);
    FLDProgressionState Holder;
    Holder.PickTickets = 1;
    Holder.SeenCards = {FName(TEXT("card_spiky_hide"))};
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    LDProgression::FShopOutcome Outcome;
    bool bGood = Reason(*this, TEXT("A ticket names any active, on the field too"), LDProgression::ApplyUsePickTicket(Content, Of, Holder,
        FName(TEXT("active_zap")), Field, Out, OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The active is learned and the ticket spent"), Outcome.Kind == ELDShopItemKind::Skill && Out.FindActive(FName(TEXT("active_zap")))
        && OutProgression.PickTickets == 0);
    bGood &= Reason(*this, TEXT("A seen card"), LDProgression::ApplyUsePickTicket(Content, Of, Holder, FName(TEXT("card_spiky_hide")), Field, Out,
        OutProgression, Outcome), ELDGrowthReason::None);
    bGood &= TestTrue(TEXT("The card joins the library"), Outcome.Kind == ELDShopItemKind::Card && Out.Cards.Num() == 1 && OutProgression.PickTickets == 0);
    bGood &= Reason(*this, TEXT("A card not seen this match"), LDProgression::ApplyUsePickTicket(Content, Of, Holder, FName(TEXT("card_tally")), Field,
        Out, OutProgression, Outcome), ELDGrowthReason::NotSeen);
    bGood &= Reason(*this, TEXT("An active already learned"), LDProgression::ApplyUsePickTicket(Content, Of, Holder, FName(TEXT("active_elbow")), Field,
        Out, OutProgression, Outcome), ELDGrowthReason::InvalidOption);
    bGood &= Reason(*this, TEXT("Something that is neither"), LDProgression::ApplyUsePickTicket(Content, Of, Holder, FName(TEXT("final_split")), Field,
        Out, OutProgression, Outcome), ELDGrowthReason::InvalidOption);
    FLDProgressionState None;
    bGood &= Reason(*this, TEXT("Without a ticket"), LDProgression::ApplyUsePickTicket(Content, Of, None, FName(TEXT("active_zap")), Field, Out,
        OutProgression, Outcome), ELDGrowthReason::NoPickTicket);
    // Card offers and shelves mark what a ticket may name.
    FLDProgressionState Seen;
    LDProgression::MarkSeenCards(Seen, {FName(TEXT("card_tally")), FName(TEXT("card_burn_guard")), FName(TEXT("card_tally"))});
    bGood &= TestTrue(TEXT("Seen cards are distinct and sorted"), Seen.SeenCards.Num() == 2 && Seen.SeenCards[0] == FName(TEXT("card_burn_guard")));
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
