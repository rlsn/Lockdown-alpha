#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDSkillGrowth.h"
#include "Dom/JsonObject.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDEconomyLedger.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDVendorDirector.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Vision/LDViewTypes.h"

namespace
{
bool GrowthTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

template <typename TEnum>
bool GrowthTestEnum(FAutomationTestBase& Test, const FString& What, TEnum Actual, TEnum Expected)
{
    return Test.TestEqual(What, UEnum::GetValueAsString(Actual), UEnum::GetValueAsString(Expected));
}

bool GrowthTestGuid(FAutomationTestBase& Test, const FString& What, const FGuid& Actual, const FGuid& Expected)
{
    return Test.TestEqual(What, Actual.ToString(), Expected.ToString());
}

// ---- Wire identities and command terms --------------------------------------------------------

// Raw bytes exactly as a client could send them: ASCII test text, at most 64 bytes, no terminator appended.
FLDDefinitionWire GrowthTestRawWire(const TCHAR* Text)
{
    FLDDefinitionWire RawBytes;
    for (int32 Index = 0; Index < int32(sizeof(RawBytes.Bytes)) && Text[Index] != 0; ++Index)
    {
        RawBytes.Bytes[Index] = uint8(Text[Index]);
    }
    return RawBytes;
}

template <typename TCommand, typename TMutation>
bool GrowthTestTermsDiffer(FAutomationTestBase& Test, const TCHAR* Family, const TCHAR* Field, const TCommand& Base, TMutation Mutate)
{
    TCommand Changed = Base;
    Mutate(Changed);
    return Test.TestFalse(FString::Printf(TEXT("%s SameTerms notices a different %s"), Family, Field),
        Base.SameTerms(Changed) || Changed.SameTerms(Base));
}

bool GrowthTestRunWire(FAutomationTestBase& Test)
{
    bool bGood = true;
    for (const TCHAR* IdText : {TEXT("item_echo_maul"), TEXT("shop"), TEXT("anchor_2")})
    {
        FLDDefinitionWire Encoded;
        FName Decoded;
        const bool bRoundTrip = FLDDefinitionWire::Encode(FName(IdText), Encoded) && Encoded.Decode(Decoded);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s encodes and decodes"), IdText), bRoundTrip);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as its ASCII bytes, a terminator and zero padding"), IdText),
            Encoded == GrowthTestRawWire(IdText));
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s decodes to the identical name"), IdText),
            Decoded == FName(IdText) && Decoded.ToString().Equals(IdText, ESearchCase::CaseSensitive));
    }

    const FString Longest = FString::ChrN(63, TEXT('a'));
    FLDDefinitionWire LongestWire;
    FName LongestName;
    bGood &= Test.TestTrue(TEXT("A 63-byte id is the longest that still fits before its terminator"),
        FLDDefinitionWire::Encode(FName(*Longest), LongestWire) && LongestWire.Bytes[62] == uint8('a') && LongestWire.Bytes[63] == 0
        && LongestWire.Decode(LongestName) && LongestName.ToString().Equals(Longest, ESearchCase::CaseSensitive));

    // FName identity ignores case and a packaged build returns the first registered casing ("Shop" for "shop");
    // the editor keeps each casing, so these variants reproduce that here.
    const TPair<const TCHAR*, const TCHAR*> CasedIds[] = {
        {TEXT("Shop"), TEXT("shop")},
        {TEXT("Growthtest_Upper"), TEXT("growthtest_upper")}};
    for (const TPair<const TCHAR*, const TCHAR*>& CasedId : CasedIds)
    {
        FLDDefinitionWire Canonical;
        FName CanonicalName;
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as the canonical bytes %s of the same name"), CasedId.Key, CasedId.Value),
            FLDDefinitionWire::Encode(FName(CasedId.Key), Canonical) && Canonical == GrowthTestRawWire(CasedId.Value)
            && Canonical.Decode(CanonicalName) && CanonicalName == FName(CasedId.Value)
            && CanonicalName.ToString().Equals(CasedId.Value, ESearchCase::CaseSensitive));
    }

    const TPair<const TCHAR*, FName> BadIds[] = {
        {TEXT("a leading digit"), FName(TEXT("2nd_item"))},
        {TEXT("a hyphen"), FName(TEXT("item-echo"))},
        {TEXT("an empty name"), FName(TEXT(""))},
        {TEXT("NAME_None"), FName(NAME_None)},
        {TEXT("a 64-byte id"), FName(*FString::ChrN(64, TEXT('a')))}};
    for (const TPair<const TCHAR*, FName>& BadId : BadIds)
    {
        FLDDefinitionWire Cleared = GrowthTestRawWire(TEXT("stale_bytes"));
        bGood &= Test.TestFalse(FString::Printf(TEXT("Encode rejects %s"), BadId.Key), FLDDefinitionWire::Encode(BadId.Value, Cleared));
        bGood &= Test.TestTrue(FString::Printf(TEXT("A rejected encode of %s leaves only zero bytes"), BadId.Key),
            Cleared == FLDDefinitionWire());
    }

    FLDDefinitionWire Unterminated;
    FMemory::Memset(Unterminated.Bytes, uint8('a'), sizeof(Unterminated.Bytes));
    FLDDefinitionWire DirtyPadding = GrowthTestRawWire(TEXT("shop"));
    DirtyPadding.Bytes[5] = uint8('x');
    FLDDefinitionWire DirtyTail = GrowthTestRawWire(TEXT("shop"));
    DirtyTail.Bytes[63] = 1;
    const TPair<const TCHAR*, FLDDefinitionWire> BadWires[] = {
        {TEXT("64 non-zero bytes without a terminator"), Unterminated},
        {TEXT("a non-zero byte right after the terminator"), DirtyPadding},
        {TEXT("a non-zero last padding byte"), DirtyTail},
        {TEXT("an uppercase byte"), GrowthTestRawWire(TEXT("sHop"))},
        {TEXT("a leading digit"), GrowthTestRawWire(TEXT("9shop"))},
        {TEXT("an empty buffer"), FLDDefinitionWire()}};
    for (const TPair<const TCHAR*, FLDDefinitionWire>& BadWire : BadWires)
    {
        FName Leftover(TEXT("shop"));
        bGood &= Test.TestFalse(FString::Printf(TEXT("Decode rejects %s"), BadWire.Key), BadWire.Value.Decode(Leftover));
        bGood &= Test.TestTrue(FString::Printf(TEXT("A rejected decode of %s yields NAME_None"), BadWire.Key), Leftover.IsNone());
    }

    bGood &= Test.TestFalse(TEXT("Unspawned cannot confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Unspawned));
    bGood &= Test.TestTrue(TEXT("Alive can confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Alive));
    bGood &= Test.TestTrue(TEXT("Dead can confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Dead));
    bGood &= Test.TestFalse(TEXT("Left cannot confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Left));

    FLDChoiceCommand Choice;
    Choice.Generation = 3;
    Choice.Life = 2;
    Choice.ExpectedLifePhase = ELDLifePhase::Dead;
    Choice.RequestId = 7;
    Choice.OfferId = FGuid(0x0FFE4000, 1, 2, 3);
    Choice.OfferRevision = 5;
    Choice.OptionId = FGuid(0x0B710000, 4, 5, 6);
    Choice.ExpectedBuildRevision = 9;
    Choice.CatalogDigest.Bytes[0] = 0xAB;
    Choice.bReplace = true;
    Choice.ReplaceSlot = 1;
    Choice.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_snark_bolt"));
    Choice.ExpectedOldRank = 2;
    Choice.bReplaceConfirmed = true;
    const TCHAR* ChoiceFamily = TEXT("Choice");
    bGood &= Test.TestTrue(TEXT("A copied choice command keeps its terms"), Choice.SameTerms(FLDChoiceCommand(Choice)));
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("Generation"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.Generation; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("Life"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.Life; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedLifePhase"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedLifePhase = ELDLifePhase::Alive; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("RequestId"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.RequestId; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OfferId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.OfferId = FGuid(0x0FFE4000, 1, 2, 4); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OfferRevision"), Choice,
        [](FLDChoiceCommand& Mutated) { ++Mutated.OfferRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OptionId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.OptionId = FGuid(0x0B710000, 4, 5, 7); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedBuildRevision"), Choice,
        [](FLDChoiceCommand& Mutated) { ++Mutated.ExpectedBuildRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("CatalogDigest"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.CatalogDigest.Bytes[31] = 0x5A; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("bReplace"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.bReplace = false; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ReplaceSlot"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ReplaceSlot = 0; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedOldSkillId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_not_mad")); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedOldRank"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedOldRank = 1; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("bReplaceConfirmed"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.bReplaceConfirmed = false; });

    FLDShopCommand Shop;
    Shop.Generation = 3;
    Shop.Life = 2;
    Shop.RequestId = 11;
    Shop.Kind = ELDShopCommandKind::Sell;
    Shop.SessionId = FGuid(0x5E550000, 1, 2, 3);
    Shop.QuoteRevision = 4;
    Shop.CatalogDigest.Bytes[0] = 0xCD;
    Shop.WorldMapId = GrowthTestRawWire(TEXT("demo_world"));
    Shop.WorldLayoutRevision = 1;
    Shop.WorldGeometryRevision = 1;
    Shop.ExpectedAccountRevision = 6;
    Shop.ExpectedInventoryRevision = 4;
    Shop.ExpectedBuildRevision = 3;
    Shop.DefinitionId = GrowthTestRawWire(TEXT("item_echo_maul"));
    Shop.BuyPrice = 400;
    Shop.Instance = FGuid(0x17E40000, 0, 0, 1);
    Shop.ExpectedItemRevision = 1;
    Shop.SellPrice = 260;
    const TCHAR* ShopFamily = TEXT("Shop");
    bGood &= Test.TestTrue(TEXT("A copied shop command keeps its terms"), Shop.SameTerms(FLDShopCommand(Shop)));
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("Generation"), Shop, [](FLDShopCommand& Mutated) { ++Mutated.Generation; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("Life"), Shop, [](FLDShopCommand& Mutated) { ++Mutated.Life; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("RequestId"), Shop, [](FLDShopCommand& Mutated) { ++Mutated.RequestId; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("Kind"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.Kind = ELDShopCommandKind::Buy; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("SessionId"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.SessionId = FGuid(0x5E550000, 1, 2, 4); });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("QuoteRevision"), Shop, [](FLDShopCommand& Mutated) { ++Mutated.QuoteRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("CatalogDigest"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.CatalogDigest.Bytes[31] = 0x5A; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("WorldMapId"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.WorldMapId = GrowthTestRawWire(TEXT("other_world")); });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("WorldLayoutRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.WorldLayoutRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("WorldGeometryRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.WorldGeometryRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("ExpectedAccountRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.ExpectedAccountRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("ExpectedInventoryRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.ExpectedInventoryRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("ExpectedBuildRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.ExpectedBuildRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("DefinitionId"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.DefinitionId = GrowthTestRawWire(TEXT("item_iron_plate")); });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("BuyPrice"), Shop, [](FLDShopCommand& Mutated) { Mutated.BuyPrice = 399; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("Instance"), Shop,
        [](FLDShopCommand& Mutated) { Mutated.Instance = FGuid(0x17E40000, 0, 0, 2); });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("ExpectedItemRevision"), Shop,
        [](FLDShopCommand& Mutated) { ++Mutated.ExpectedItemRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ShopFamily, TEXT("SellPrice"), Shop, [](FLDShopCommand& Mutated) { Mutated.SellPrice = 259; });
    return bGood;
}

// ---- Ledger -----------------------------------------------------------------------------------

LDEconomy::FTransaction GrowthTestTransaction(LDEconomy::ECause Cause, uint32 Key)
{
    LDEconomy::FTransaction Transaction;
    Transaction.Cause = Cause;
    Transaction.BusinessKey = FGuid(0x6B657900, 0, 0, Key);
    Transaction.CommitBatch = Key;
    for (int32 Index = 0; Index < int32(sizeof(Transaction.Terms)); ++Index)
    {
        Transaction.Terms[Index] = uint8(Key * 31 + uint32(Index));
    }
    return Transaction;
}

LDEconomy::FTransaction GrowthTestReward(uint32 Key, const FGuid& AccountId, uint64 AccountRevision, int64 Gold)
{
    LDEconomy::FTransaction Transaction = GrowthTestTransaction(LDEconomy::ECause::DeathReward, Key);
    Transaction.Mint = Gold;
    Transaction.Gold = LDEconomy::FGoldChange{AccountId, AccountRevision, Gold};
    return Transaction;
}

LDEconomy::FTransaction GrowthTestBuy(uint32 Key, const FGuid& AccountId, uint64 AccountRevision, uint64 InventoryRevision,
    FName Definition, uint8 Slot, int64 Price)
{
    LDEconomy::FTransaction Transaction = GrowthTestTransaction(LDEconomy::ECause::Buy, Key);
    Transaction.Burn = Price;
    Transaction.Gold = LDEconomy::FGoldChange{AccountId, AccountRevision, -Price};
    Transaction.Create = LDEconomy::FCreateItem{AccountId, InventoryRevision, Definition, Slot};
    return Transaction;
}

LDEconomy::FTransaction GrowthTestSell(uint32 Key, const FGuid& AccountId, uint64 AccountRevision, uint64 InventoryRevision,
    const FGuid& Instance, uint64 ItemRevision, int64 Gold)
{
    LDEconomy::FTransaction Transaction = GrowthTestTransaction(LDEconomy::ECause::Sell, Key);
    Transaction.Mint = Gold;
    Transaction.Gold = LDEconomy::FGoldChange{AccountId, AccountRevision, Gold};
    Transaction.Consume = LDEconomy::FConsumeItem{Instance, ItemRevision, AccountId, InventoryRevision};
    return Transaction;
}

LDEconomy::FTransaction GrowthTestRetire(uint32 Key, const FGuid& AccountId, uint64 AccountRevision, uint32 Life, uint64 LifeRevision)
{
    LDEconomy::FTransaction Transaction = GrowthTestTransaction(LDEconomy::ECause::AccountRetire, Key);
    Transaction.Retire = LDEconomy::FRetireAccount{AccountId, AccountRevision, LDEconomy::FRetireStamp{Life, LifeRevision}};
    return Transaction;
}

bool GrowthTestCommit(FAutomationTestBase& Test, LDEconomy::FLedger& Ledger, const LDEconomy::FTransaction& Transaction,
    const TCHAR* What, LDEconomy::FJournalEntry* OutEntry = nullptr)
{
    LDEconomy::FPrepareResult Result = Ledger.Prepare(Transaction);
    if (!Test.TestTrue(FString::Printf(TEXT("%s prepares (%s: %s)"), What, LDEconomy::FLedger::ErrorName(Result.Error), *Result.Message),
        Result.IsSuccess()))
    {
        return false;
    }
    if (OutEntry)
    {
        *OutEntry = Result.Value->Entry();
    }
    Ledger.ApplyPrepared(MoveTemp(*Result.Value));
    return true;
}

bool GrowthTestUnchanged(FAutomationTestBase& Test, const LDEconomy::FAuditState& Before, const LDEconomy::FLedger& Ledger,
    const FString& What)
{
    FString Difference;
    const bool bSame = LDEconomy::SameState(Before, Ledger.State(), Difference);
    return Test.TestTrue(FString::Printf(TEXT("%s leaves the ledger unchanged (%s)"), *What, *Difference), bSame);
}

bool GrowthTestRefuse(FAutomationTestBase& Test, const LDEconomy::FLedger& Ledger, const LDEconomy::FTransaction& Transaction,
    LDEconomy::EPrepareError Expected, const TCHAR* What)
{
    const LDEconomy::FAuditState Before = Ledger.State();
    const int32 TailBefore = Ledger.JournalTail().Num();
    const LDEconomy::FPrepareResult Result = Ledger.Prepare(Transaction);
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s is refused"), What), LDEconomy::FLedger::ErrorName(Result.Error),
        LDEconomy::FLedger::ErrorName(Expected));
    bGood &= Test.TestFalse(FString::Printf(TEXT("%s yields no plan"), What), Result.Value.IsSet());
    bGood &= Test.TestFalse(FString::Printf(TEXT("%s explains the refusal"), What), Result.Message.IsEmpty());
    bGood &= GrowthTestUnchanged(Test, Before, Ledger, What);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s adds no journal entry"), What), Ledger.JournalTail().Num(), TailBefore);
    return bGood;
}

const TCHAR* GrowthTestCompactName(LDEconomy::ECompactResult Result)
{
    switch (Result)
    {
    case LDEconomy::ECompactResult::Compacted: return TEXT("Compacted");
    case LDEconomy::ECompactResult::NothingToCompact: return TEXT("NothingToCompact");
    case LDEconomy::ECompactResult::Pinned: return TEXT("Pinned");
    case LDEconomy::ECompactResult::PrepareUnavailable: return TEXT("PrepareUnavailable");
    case LDEconomy::ECompactResult::CorruptHistory: return TEXT("CorruptHistory");
    }
    return TEXT("Unknown");
}

bool GrowthTestCompact(FAutomationTestBase& Test, LDEconomy::FLedger& Ledger, uint64 ExpectedSerial, LDEconomy::ECompactResult Expected,
    const TCHAR* What)
{
    FString Error;
    const LDEconomy::ECompactResult Result = Ledger.TryCompactResolvedPrefix(ExpectedSerial, Error);
    return Test.TestEqual(FString::Printf(TEXT("%s (%s)"), What, *Error), GrowthTestCompactName(Result), GrowthTestCompactName(Expected));
}

bool GrowthTestExpectAccount(FAutomationTestBase& Test, const LDEconomy::FLedger& Ledger, const FGuid& AccountId, int64 Gold,
    uint64 Revision, const TCHAR* What)
{
    const LDEconomy::FAccount* Account = Ledger.Account(AccountId);
    if (!Test.TestNotNull(FString::Printf(TEXT("%s: account exists"), What), Account))
    {
        return false;
    }
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s: gold"), What), Account->Gold, Gold);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: account revision"), What), Account->Revision, Revision);
    return bGood;
}

// Zero is never a live revision, so a missing account surfaces as a refused commit rather than a success.
uint64 GrowthTestAccountRevision(const LDEconomy::FLedger& Ledger, const FGuid& AccountId)
{
    const LDEconomy::FAccount* Account = Ledger.Account(AccountId);
    return Account ? Account->Revision : 0;
}

bool GrowthTestReconciles(FAutomationTestBase& Test, const LDEconomy::FLedger& Ledger, const TCHAR* What)
{
    FString Error;
    const bool bReconciled = Ledger.Reconcile(Error);
    return Test.TestTrue(FString::Printf(TEXT("%s: independent replay reconciles (%s)"), What, *Error), bReconciled);
}

bool GrowthTestConserves(FAutomationTestBase& Test, const LDEconomy::FLedger& Ledger, const TCHAR* What)
{
    const LDEconomy::FAuditState& State = Ledger.State();
    return Test.TestEqual(FString::Printf(TEXT("%s: total gold equals initial + minted - burned"), What), Ledger.TotalGold(),
        State.InitialGoldTotal + State.Minted - State.Burned);
}

bool GrowthTestRunLedger(FAutomationTestBase& Test)
{
    // Approved values (CONTENT_AND_BALANCE §5/§6.1): SHOP.StartingGold 320, item_iron_plate 310 / 201, item_siphon_emblem 360,
    // a level 10 contender kill pays 170 gold and a monster_scavenger 30.
    const int64 StartingGold = 320;
    const int64 PlatePrice = 310;
    const int64 PlateSellGold = 201;
    const int64 EmblemPrice = 360;
    const FName Plate(TEXT("item_iron_plate"));
    const FName Emblem(TEXT("item_siphon_emblem"));
    const FGuid Match(0x4D415443, 1, 2, 3);
    const FGuid P1(0x0000A001, 0, 0, 1);
    const FGuid P2(0x0000A002, 0, 0, 2);
    const FGuid P3(0x0000A003, 0, 0, 3);
    bool bGood = true;

    LDEconomy::FLedger Ledger;
    FString Error;
    const bool bInitialized = Ledger.Initialize(Match, {P1, P2, P3}, StartingGold, Error);
    if (!Test.TestTrue(FString::Printf(TEXT("Three roster accounts initialize (%s)"), *Error), bInitialized))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Initial gold is 3 x 320"), Ledger.TotalGold(), int64(960));
    bGood &= Test.TestEqual(TEXT("The initial gold total is recorded for reconciliation"), Ledger.State().InitialGoldTotal, int64(960));
    for (const FGuid& RosterAccount : {P1, P2, P3})
    {
        bGood &= GrowthTestExpectAccount(Test, Ledger, RosterAccount, StartingGold, 1, TEXT("A freshly initialized account"));
        bGood &= Test.TestEqual(TEXT("A freshly initialized inventory revision"), Ledger.InventoryRevision(RosterAccount), uint64(1));
    }
    bGood &= Test.TestEqual(TEXT("Nothing is committed yet"), Ledger.CommittedSerial(), uint64(0));
    bGood &= Test.TestEqual(TEXT("The first commit will be serial 1"), Ledger.NextCommitSerial(), uint64(1));
    bGood &= Test.TestEqual(TEXT("An account outside the roster has no inventory"), Ledger.InventoryRevision(FGuid(0x0000A004, 0, 0, 4)),
        uint64(0));
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("The initialized ledger"));

    const LDEconomy::FAuditState Initial = Ledger.State();
    bGood &= Test.TestFalse(TEXT("A second Initialize cannot refill a running match"),
        Ledger.Initialize(Match, {P1, P2, P3}, StartingGold, Error));
    bGood &= GrowthTestUnchanged(Test, Initial, Ledger, TEXT("The refused second Initialize"));
    {
        LDEconomy::FLedger Roster;
        bGood &= Test.TestFalse(TEXT("A duplicate roster account is refused"), Roster.Initialize(Match, {P1, P1}, StartingGold, Error));
        bGood &= Test.TestFalse(TEXT("An invalid roster account is refused"), Roster.Initialize(Match, {P1, FGuid()}, StartingGold, Error));
        bGood &= Test.TestFalse(TEXT("An invalid match identity is refused"), Roster.Initialize(FGuid(), {P1}, StartingGold, Error));
        bGood &= Test.TestFalse(TEXT("Refused initializations leave the ledger unbound"), Roster.IsInitialized());
    }

    const LDEconomy::FTransaction FirstReward = GrowthTestReward(1, P1, 1, 80);
    {
        const LDEconomy::FAuditState BeforeReward = Ledger.State();
        LDEconomy::FPrepareResult Plan = Ledger.Prepare(FirstReward);
        if (!Test.TestTrue(FString::Printf(TEXT("The 80 gold death reward prepares (%s)"), *Plan.Message), Plan.IsSuccess()))
        {
            return false;
        }
        bGood &= GrowthTestUnchanged(Test, BeforeReward, Ledger, TEXT("Preparing the reward"));
        bGood &= Test.TestEqual(TEXT("The reward plan carries serial 1"), Plan.Value->Entry().Serial, uint64(1));
        Ledger.ApplyPrepared(MoveTemp(*Plan.Value));
    }
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 400, 2, TEXT("After the 80 gold death reward"));
    bGood &= Test.TestEqual(TEXT("The reward is committed as serial 1"), Ledger.CommittedSerial(), uint64(1));
    bGood &= Test.TestEqual(TEXT("The next commit will be serial 2"), Ledger.NextCommitSerial(), uint64(2));
    bGood &= Test.TestEqual(TEXT("The reward is classified as minted gold"), Ledger.State().Minted, int64(80));
    bGood &= Test.TestEqual(TEXT("A reward leaves the inventory revision"), Ledger.InventoryRevision(P1), uint64(1));
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("After the reward"));

    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(50, P2, 1, 1, Emblem, 0, EmblemPrice), LDEconomy::EPrepareError::InsufficientGold,
        TEXT("Buying the 360 gold siphon emblem with the 320 starting gold"));

    for (uint32 Key = 2; Key <= 6; ++Key)
    {
        if (!GrowthTestCommit(Test, Ledger, GrowthTestReward(Key, P1, GrowthTestAccountRevision(Ledger, P1), 170),
            TEXT("A level 10 kill reward")))
        {
            return false;
        }
    }
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 1250, 7, TEXT("After five 170 gold rewards"));

    const FGuid FirstPlate = LDEconomy::FLedger::InstanceFor(Match, 1);
    const FGuid SecondPlate = LDEconomy::FLedger::InstanceFor(Match, 2);
    LDEconomy::FJournalEntry BuyEntry;
    if (!GrowthTestCommit(Test, Ledger, GrowthTestBuy(7, P1, 7, 1, Plate, 0, PlatePrice), TEXT("Buying a plate into slot 0"), &BuyEntry))
    {
        return false;
    }
    bGood &= GrowthTestGuid(Test, TEXT("The purchase issues InstanceFor(Match, 1)"), BuyEntry.CreatedInstance, FirstPlate);
    bGood &= Test.TestEqual(TEXT("The purchase records issue serial 1"), BuyEntry.CreatedIssueSerial, uint64(1));
    const LDEconomy::FItem* Bought = Ledger.Item(FirstPlate);
    if (!Test.TestNotNull(TEXT("The purchased instance is live"), Bought))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("The purchase fills the requested slot"), int32(Bought->Slot), 0);
    bGood &= Test.TestEqual(TEXT("The purchased definition"), Bought->Definition, Plate);
    bGood &= GrowthTestGuid(Test, TEXT("The purchased owner"), Bought->Owner, P1);
    bGood &= Test.TestEqual(TEXT("A new instance starts at item revision 1"), Bought->Revision, uint64(1));
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 940, 8, TEXT("After buying the first plate"));
    bGood &= Test.TestEqual(TEXT("The purchase bumps the inventory revision"), Ledger.InventoryRevision(P1), uint64(2));
    bGood &= Test.TestEqual(TEXT("The purchase price is burned"), Ledger.State().Burned, int64(310));
    bGood &= Test.TestEqual(TEXT("One instance has been issued"), Ledger.IssuedThrough(), uint64(1));

    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(51, P1, 8, 2, Plate, 0, PlatePrice), LDEconomy::EPrepareError::SlotOccupied,
        TEXT("Buying into occupied slot 0 while slot 1 is free"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(52, P1, 7, 2, Plate, 1, PlatePrice), LDEconomy::EPrepareError::AccountStale,
        TEXT("Buying with a stale account revision"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(53, P1, 8, 1, Plate, 1, PlatePrice), LDEconomy::EPrepareError::InventoryStale,
        TEXT("Buying with a stale inventory revision"));

    if (!GrowthTestCommit(Test, Ledger, GrowthTestBuy(8, P1, 8, 2, Plate, 1, PlatePrice),
        TEXT("Buying a second identical plate into slot 1")))
    {
        return false;
    }
    TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
    Ledger.OwnerItems(P1, Owned);
    bGood &= Test.TestTrue(TEXT("Two identical plates are held in slot order"), Owned.Num() == 2 && Owned[0].Key == FirstPlate
        && Owned[1].Key == SecondPlate && Owned[0].Value.Slot == 0 && Owned[1].Value.Slot == 1
        && Owned[0].Value.Definition == Plate && Owned[1].Value.Definition == Plate);
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 630, 9, TEXT("After buying the second plate"));
    bGood &= Test.TestEqual(TEXT("The second purchase bumps the inventory revision"), Ledger.InventoryRevision(P1), uint64(3));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(54, P1, 9, 3, Plate, 0, PlatePrice), LDEconomy::EPrepareError::EquipmentFull,
        TEXT("A third affordable purchase with both slots used"));

    LDEconomy::FJournalEntry SellEntry;
    if (!GrowthTestCommit(Test, Ledger, GrowthTestSell(9, P1, 9, 3, FirstPlate, 1, PlateSellGold), TEXT("Selling the slot 0 plate"),
        &SellEntry))
    {
        return false;
    }
    bGood &= Test.TestNull(TEXT("The sale consumes exactly the sold instance"), Ledger.Item(FirstPlate));
    bGood &= Test.TestNotNull(TEXT("The sale keeps the other plate"), Ledger.Item(SecondPlate));
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 831, 10, TEXT("After selling for 201"));
    bGood &= Test.TestEqual(TEXT("The sale bumps the inventory revision"), Ledger.InventoryRevision(P1), uint64(4));
    bGood &= Test.TestEqual(TEXT("Sale gold is minted"), Ledger.State().Minted, int64(1131));
    bGood &= Test.TestFalse(TEXT("A sale issues no instance"), SellEntry.CreatedInstance.IsValid());
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("After buying and selling"));

    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestSell(55, P1, 10, 4, FirstPlate, 1, PlateSellGold),
        LDEconomy::EPrepareError::UnknownItem, TEXT("Selling the consumed plate again"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestSell(56, P1, 10, 4, SecondPlate, 2, PlateSellGold),
        LDEconomy::EPrepareError::ItemStale, TEXT("Selling with a stale item revision"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestSell(57, P2, 1, 1, SecondPlate, 1, PlateSellGold), LDEconomy::EPrepareError::NotOwner,
        TEXT("Selling another contender's plate"));

    bGood &= GrowthTestRefuse(Test, Ledger, FirstReward, LDEconomy::EPrepareError::DuplicateBusinessKey,
        TEXT("Re-preparing the committed first reward"));
    LDEconomy::FTransaction Conflicting = FirstReward;
    Conflicting.Terms[0] = uint8(~Conflicting.Terms[0]);
    bGood &= GrowthTestRefuse(Test, Ledger, Conflicting, LDEconomy::EPrepareError::BusinessKeyConflict,
        TEXT("The first reward's business key with different terms"));

    const LDEconomy::EPrepareError InvalidShape = LDEconomy::EPrepareError::InvalidTransaction;
    LDEconomy::FTransaction Shape = GrowthTestBuy(60, P1, 10, 4, Plate, 0, PlatePrice);
    Shape.Create.Reset();
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A purchase without a created item"));
    Shape = GrowthTestReward(61, P1, 10, 80);
    Shape.Burn = 80;
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A death reward that also burns"));
    Shape = GrowthTestReward(62, P1, 10, 80);
    Shape.Mint = 79;
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A death reward whose mint differs from its gold"));
    Shape = GrowthTestSell(63, P1, 10, 4, SecondPlate, 1, PlateSellGold);
    Shape.Create = LDEconomy::FCreateItem{P1, 4, Plate, 0};
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A sale that also creates an item"));
    Shape = GrowthTestBuy(64, P1, 10, 4, Plate, 0, PlatePrice);
    Shape.Create->Owner = P2;
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A purchase delivered to another account"));
    Shape = GrowthTestRetire(65, P2, 1, 2, 7);
    Shape.Gold = LDEconomy::FGoldChange{P2, 1, 10};
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A retirement that also moves gold"));
    Shape = GrowthTestReward(66, P1, 10, 80);
    Shape.Cause = LDEconomy::ECause::None;
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A transaction without a cause"));
    Shape = GrowthTestReward(67, P1, 10, 80);
    Shape.BusinessKey = FGuid();
    bGood &= GrowthTestRefuse(Test, Ledger, Shape, InvalidShape, TEXT("A transaction without a business key"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestRetire(68, P2, 1, 0, 0), InvalidShape,
        TEXT("A retirement without the core leave stamp"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(69, P1, 10, 4, Plate, 2, PlatePrice), InvalidShape,
        TEXT("A purchase into slot 2"));

    const int64 MintedBeforeRetire = Ledger.State().Minted;
    const int64 BurnedBeforeRetire = Ledger.State().Burned;
    LDEconomy::FJournalEntry RetireEntry;
    if (!GrowthTestCommit(Test, Ledger, GrowthTestRetire(10, P3, 1, 2, 7), TEXT("Retiring a leaving contender's account"), &RetireEntry))
    {
        return false;
    }
    const LDEconomy::FAccount* Retired = Ledger.Account(P3);
    if (!Test.TestNotNull(TEXT("A retired account stays in the ledger"), Retired))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("Retirement marks the account retired"), Retired->bRetired);
    bGood &= Test.TestEqual(TEXT("Retirement bumps the account revision"), Retired->Revision, uint64(2));
    bGood &= Test.TestEqual(TEXT("Retirement is commit serial 10"), RetireEntry.Serial, uint64(10));
    bGood &= Test.TestEqual(TEXT("RetireSerial is the retirement's own commit serial"), Retired->RetireSerial, RetireEntry.Serial);
    bGood &= Test.TestTrue(TEXT("Retirement keeps the core leave stamp"), Retired->RetireStamp == LDEconomy::FRetireStamp{2, 7});
    bGood &= Test.TestEqual(TEXT("Retirement does not change gold"), Retired->Gold, StartingGold);
    bGood &= Test.TestTrue(TEXT("Retirement neither mints nor burns"),
        Ledger.State().Minted == MintedBeforeRetire && Ledger.State().Burned == BurnedBeforeRetire);
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestReward(70, P3, 2, 80), LDEconomy::EPrepareError::RetiredAccount,
        TEXT("A death reward to a retired account"));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestRetire(71, P3, 2, 2, 7), LDEconomy::EPrepareError::AlreadyRetired,
        TEXT("Retiring an account twice"));

    if (!Test.TestEqual(TEXT("Ten commits precede the journal fill"), Ledger.JournalTail().Num(), 10))
    {
        return false;
    }
    uint32 FillKey = 100;
    while (Ledger.JournalTail().Num() < LDEconomy::JournalTailCapacity)
    {
        const uint32 Key = FillKey++;
        if (!GrowthTestCommit(Test, Ledger, GrowthTestReward(Key, P2, GrowthTestAccountRevision(Ledger, P2), 30),
            TEXT("A journal filler reward")))
        {
            return false;
        }
    }
    bGood &= Test.TestEqual(TEXT("The journal tail holds its 64-entry capacity"), Ledger.JournalTail().Num(),
        LDEconomy::JournalTailCapacity);
    bGood &= Test.TestEqual(TEXT("The full tail ends at serial 64"), Ledger.CommittedSerial(), uint64(64));
    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestReward(FillKey, P2, GrowthTestAccountRevision(Ledger, P2), 30),
        LDEconomy::EPrepareError::JournalFull, TEXT("A 65th prepare before compaction"));

    bGood &= GrowthTestCompact(Test, Ledger, 63, LDEconomy::ECompactResult::Pinned, TEXT("Compaction with a stale expected serial"));
    Ledger.Pin();
    bGood &= GrowthTestCompact(Test, Ledger, 64, LDEconomy::ECompactResult::Pinned,
        TEXT("Compaction while a composite commit pins the ledger"));
    Ledger.Unpin();
    bGood &= Test.TestEqual(TEXT("Refused compactions keep the tail"), Ledger.JournalTail().Num(), LDEconomy::JournalTailCapacity);
    const LDEconomy::FAuditState BeforeCompaction = Ledger.State();
    if (!GrowthTestCompact(Test, Ledger, Ledger.CommittedSerial(), LDEconomy::ECompactResult::Compacted,
        TEXT("Compacting the resolved tail")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Compaction empties the journal tail"), Ledger.JournalTail().Num(), 0);
    bGood &= Test.TestEqual(TEXT("Compaction keeps the committed serial"), Ledger.CommittedSerial(), uint64(64));
    bGood &= GrowthTestUnchanged(Test, BeforeCompaction, Ledger, TEXT("Compaction"));
    FString PrefixDifference;
    const bool bPrefixIsLive = LDEconomy::SameState(Ledger.AuditPrefix(), Ledger.State(), PrefixDifference);
    bGood &= Test.TestTrue(FString::Printf(TEXT("The new audit prefix equals the live state (%s)"), *PrefixDifference), bPrefixIsLive);
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("After compaction"));
    bGood &= GrowthTestCompact(Test, Ledger, 64, LDEconomy::ECompactResult::NothingToCompact, TEXT("Compacting an empty tail"));
    const LDEconomy::FAccount* StillRetired = Ledger.Account(P3);
    bGood &= Test.TestTrue(TEXT("Compaction keeps the account retired at serial 10 with its gold"),
        StillRetired && StillRetired->bRetired && StillRetired->RetireSerial == 10 && StillRetired->Gold == StartingGold);
    bGood &= GrowthTestConserves(Test, Ledger, TEXT("After compaction"));
    bGood &= Test.TestEqual(TEXT("Total gold after compaction"), Ledger.TotalGold(), int64(3091));
    bGood &= Test.TestTrue(TEXT("Mint and burn totals survive compaction"), Ledger.State().Minted == 2751 && Ledger.State().Burned == 620);

    for (const uint64 ExpectedSerial : {uint64(65), uint64(66)})
    {
        LDEconomy::FJournalEntry Entry;
        const uint32 Key = FillKey++;
        if (!GrowthTestCommit(Test, Ledger, GrowthTestReward(Key, P2, GrowthTestAccountRevision(Ledger, P2), 30),
            TEXT("A reward after compaction"), &Entry))
        {
            return false;
        }
        bGood &= Test.TestEqual(FString::Printf(TEXT("A post-compaction commit gets serial %llu"), ExpectedSerial), Entry.Serial,
            ExpectedSerial);
        bGood &= Test.TestEqual(TEXT("The committed serial follows the new entry"), Ledger.CommittedSerial(), ExpectedSerial);
    }
    bGood &= Test.TestEqual(TEXT("Two entries follow the compacted prefix"), Ledger.JournalTail().Num(), 2);
    bGood &= GrowthTestExpectAccount(Test, Ledger, P2, 2000, 57, TEXT("The filler account at the end"));
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("After post-compaction commits"));
    bGood &= GrowthTestConserves(Test, Ledger, TEXT("The final ledger"));
    return bGood;
}

// A compacted prefix followed by every cause; -LDGrowthLedgerEvidence=<file> keeps the export for
// Scripts\CheckEconomyEvidence.py so the independent replay is checked against real native output.
bool GrowthTestRunLedgerEvidence(FAutomationTestBase& Test)
{
    const FGuid Match(0x11111111, 0x22223333, 0x44445555, 0x66667777);
    const FGuid P1(0xE1, 0, 0, 1);
    const FGuid P2(0xE1, 0, 0, 2);
    const FGuid P3(0xE1, 0, 0, 3);
    const FName Blade(TEXT("item_edge_blade"));
    // The same definition under another FName casing, as a packaged build may hold it.
    const FName BladeCased(TEXT("Item_Edge_Blade"));
    const int64 StartingGold = 320;
    LDEconomy::FLedger Ledger;
    FString Json;
    FString Error;
    bool bGood = Test.TestFalse(TEXT("An uninitialized ledger exports no evidence"),
        LDEconomy::ExportEvidence(Ledger, StartingGold, Json, Error));
    bGood &= Test.TestTrue(TEXT("The refused export explains itself and returns no text"), !Error.IsEmpty() && Json.IsEmpty());
    if (!Test.TestTrue(FString::Printf(TEXT("The evidence ledger initializes (%s)"), *Error),
        Ledger.Initialize(Match, {P1, P2, P3}, StartingGold, Error)))
    {
        return false;
    }
    // Folded into the audit prefix: P2 buys one item and P3 leaves.
    if (!GrowthTestCommit(Test, Ledger, GrowthTestBuy(201, P2, 1, 1, Blade, 0, 100), TEXT("The evidence prefix buy"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestRetire(202, P3, 1, 2, 7), TEXT("The evidence prefix retirement"))
        || !GrowthTestCompact(Test, Ledger, 2, LDEconomy::ECompactResult::Compacted, TEXT("The evidence prefix compaction")))
    {
        return false;
    }
    const FGuid PrefixItem = LDEconomy::FLedger::InstanceFor(Match, 1);
    const FGuid FirstBlade = LDEconomy::FLedger::InstanceFor(Match, 2);
    // The tail covers every cause, two instances of one definition and the sale of a prefix item.
    if (!GrowthTestCommit(Test, Ledger, GrowthTestReward(203, P1, 1, 80), TEXT("The evidence tail reward"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestBuy(204, P1, 2, 1, Blade, 0, 100), TEXT("The evidence tail first buy"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestBuy(205, P1, 3, 2, BladeCased, 1, 100), TEXT("The evidence tail second buy"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestSell(206, P1, 4, 3, FirstBlade, 1, 65), TEXT("The evidence tail sale"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestSell(207, P2, 2, 2, PrefixItem, 1, 65), TEXT("The sale of a prefix item"))
        || !GrowthTestCommit(Test, Ledger, GrowthTestRetire(208, P2, 3, 1, 3), TEXT("The evidence tail retirement")))
    {
        return false;
    }
    bGood &= GrowthTestConserves(Test, Ledger, TEXT("The evidence ledger"));
    if (!Test.TestTrue(FString::Printf(TEXT("The ledger exports its evidence (%s)"), *Error),
        LDEconomy::ExportEvidence(Ledger, StartingGold, Json, Error)))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (!Test.TestTrue(TEXT("The evidence is one JSON object"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The evidence names schema 5, its match and the approved starting gold"),
        Root->GetIntegerField(TEXT("schemaVersion")) == 5
        && Root->GetStringField(TEXT("match")) == Match.ToString(EGuidFormats::DigitsWithHyphensLower)
        && Root->GetStringField(TEXT("startingGold")) == TEXT("320"));
    const TSharedPtr<FJsonObject>* FixtureBlock = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* FixtureGrants = nullptr;
    bGood &= Test.TestTrue(TEXT("Without -LDDevGrowthFixture the evidence states the fixture was off and lists no DevFixture mint"),
        Root->TryGetObjectField(TEXT("devFixture"), FixtureBlock) && !(*FixtureBlock)->GetBoolField(TEXT("enabled"))
        && (*FixtureBlock)->TryGetArrayField(TEXT("grants"), FixtureGrants) && FixtureGrants->IsEmpty()
        && (*FixtureBlock)->GetStringField(TEXT("minted")) == TEXT("0"));
    const TArray<TSharedPtr<FJsonValue>>* Tail = nullptr;
    const TSharedPtr<FJsonObject>* Prefix = nullptr;
    const TSharedPtr<FJsonObject>* Current = nullptr;
    if (!Test.TestTrue(TEXT("The evidence carries the prefix, the tail and the live state"),
        Root->TryGetArrayField(TEXT("tail"), Tail) && Root->TryGetObjectField(TEXT("prefix"), Prefix)
        && Root->TryGetObjectField(TEXT("current"), Current) && Tail->Num() == 6))
    {
        return false;
    }
    TArray<FString> Causes;
    for (const TSharedPtr<FJsonValue>& Entry : *Tail)
    {
        Causes.Add(Entry->AsObject()->GetStringField(TEXT("cause")));
    }
    bGood &= Test.TestEqual(TEXT("The tail lists every cause in commit order"), FString::Join(Causes, TEXT(",")),
        FString(TEXT("DeathReward,Buy,Buy,Sell,Sell,AccountRetire")));
    bGood &= Test.TestTrue(TEXT("The prefix ends at serial 2 and the live state at serial 8"),
        (*Prefix)->GetStringField(TEXT("throughSerial")) == TEXT("2") && (*Current)->GetStringField(TEXT("throughSerial")) == TEXT("8"));
    bGood &= Test.TestEqual(TEXT("The first tail buy records its issued instance"),
        (*Tail)[1]->AsObject()->GetStringField(TEXT("createdInstance")), FirstBlade.ToString(EGuidFormats::DigitsWithHyphensLower));
    TArray<FString> Definitions;
    for (const TSharedPtr<FJsonValue>& Entry : *Tail)
    {
        const TSharedPtr<FJsonObject>* Create = nullptr;
        if (Entry->AsObject()->TryGetObjectField(TEXT("create"), Create))
        {
            Definitions.Add((*Create)->GetStringField(TEXT("definition")));
        }
    }
    for (const TSharedPtr<FJsonObject>* State : {Prefix, Current})
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if ((*State)->TryGetArrayField(TEXT("items"), Items))
        {
            for (const TSharedPtr<FJsonValue>& Item : *Items)
            {
                Definitions.Add(Item->AsObject()->GetStringField(TEXT("definition")));
            }
        }
    }
    bGood &= Test.TestEqual(TEXT("Both creates, the prefix item and the live item export the canonical lowercase id, whatever casing was committed"),
        FString::Join(Definitions, TEXT(",")), FString(TEXT("item_edge_blade,item_edge_blade,item_edge_blade,item_edge_blade")));
    FString EvidencePath;
    if (FParse::Value(FCommandLine::Get(), TEXT("LDGrowthLedgerEvidence="), EvidencePath))
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("The evidence is written to %s"), *EvidencePath),
            !EvidencePath.IsEmpty() && FFileHelper::SaveStringToFile(Json, *EvidencePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    return bGood;
}

// DEV growth fixture mints: DeathReward's shape under their own cause, listed for the evidence even after compaction.
bool GrowthTestRunDevFixtureLedger(FAutomationTestBase& Test)
{
    const FGuid Match(0x22222222, 0x33334444, 0x55556666, 0x77778888);
    const FGuid P1(0xE2, 0, 0, 1);
    const FGuid P2(0xE2, 0, 0, 2);
    LDEconomy::FLedger Ledger;
    FString Error;
    if (!Test.TestTrue(TEXT("The DevFixture ledger initializes"), Ledger.Initialize(Match, {P1, P2}, 150, Error)))
    {
        return false;
    }
    LDEconomy::FTransaction Grant = GrowthTestTransaction(LDEconomy::ECause::DevFixture, 301);
    Grant.Mint = 50;
    Grant.Gold = LDEconomy::FGoldChange{P1, 1, 50};
    LDEconomy::FTransaction Burning = Grant;
    Burning.BusinessKey = FGuid(0x6B657900, 0, 0, 302);
    Burning.Burn = 50;
    bool bGood = GrowthTestRefuse(Test, Ledger, Burning, LDEconomy::EPrepareError::InvalidTransaction,
        TEXT("A DevFixture mint that also burns"));
    LDEconomy::FTransaction Charging = Grant;
    Charging.BusinessKey = FGuid(0x6B657900, 0, 0, 303);
    Charging.Mint = 0;
    Charging.Burn = 50;
    Charging.Gold->Delta = -50;
    bGood &= GrowthTestRefuse(Test, Ledger, Charging, LDEconomy::EPrepareError::InvalidTransaction, TEXT("A DevFixture that charges gold"));
    LDEconomy::FJournalEntry Entry;
    if (!GrowthTestCommit(Test, Ledger, Grant, TEXT("A DevFixture mint"), &Entry))
    {
        return false;
    }
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 200, 2, TEXT("The DevFixture recipient"));
    const TArray<LDEconomy::FDevFixtureMint>& Mints = Ledger.DevFixtureGrants();
    bGood &= Test.TestTrue(TEXT("The ledger lists the DevFixture mint by serial, account and gold"),
        Mints.Num() == 1 && Mints[0].Serial == Entry.Serial && Mints[0].Account == P1 && Mints[0].Gold == 50);
    bGood &= GrowthTestCompact(Test, Ledger, Ledger.CommittedSerial(), LDEconomy::ECompactResult::Compacted,
        TEXT("Compacting the DevFixture mint"));
    bGood &= Test.TestTrue(TEXT("Compaction folds the journal entry but keeps the DevFixture listing"),
        Ledger.JournalTail().IsEmpty() && Ledger.DevFixtureGrants().Num() == 1);
    FString Json;
    TSharedPtr<FJsonObject> Root;
    const TSharedPtr<FJsonObject>* Fixture = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Grants = nullptr;
    if (!Test.TestTrue(FString::Printf(TEXT("The DevFixture ledger exports evidence (%s)"), *Error),
        LDEconomy::ExportEvidence(Ledger, 150, Json, Error) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
        && Root.IsValid() && Root->TryGetObjectField(TEXT("devFixture"), Fixture) && (*Fixture)->TryGetArrayField(TEXT("grants"), Grants)))
    {
        return false;
    }
    bGood &= Test.TestFalse(TEXT("The rules run has no -LDDevGrowthFixture, so the evidence says the fixture was off"),
        (*Fixture)->GetBoolField(TEXT("enabled")));
    TSharedPtr<FJsonObject> Row;
    if (Grants->Num() == 1)
    {
        Row = (*Grants)[0]->AsObject();
    }
    bGood &= Test.TestTrue(TEXT("The evidence lists the folded DevFixture mint and its total"),
        Row.IsValid() && Row->GetStringField(TEXT("serial")) == LexToString(Entry.Serial)
        && Row->GetStringField(TEXT("account")) == P1.ToString(EGuidFormats::DigitsWithHyphensLower)
        && Row->GetStringField(TEXT("gold")) == TEXT("50") && (*Fixture)->GetStringField(TEXT("minted")) == TEXT("50"));
    return bGood;
}

// ---- Request streams --------------------------------------------------------------------------

FLDChoiceCommand GrowthTestChoiceCommand(uint64 RequestId)
{
    FLDChoiceCommand Command;
    Command.Generation = 4;
    Command.Life = 2;
    Command.ExpectedLifePhase = ELDLifePhase::Alive;
    Command.RequestId = RequestId;
    Command.OfferId = FGuid(0x0FFE4000, 0, 0, uint32(RequestId));
    Command.OfferRevision = RequestId;
    Command.OptionId = FGuid(0x0B710000, 0, 0, uint32(RequestId));
    Command.ExpectedBuildRevision = 1;
    return Command;
}

FLDChoiceReceipt GrowthTestChoiceReceipt(const FLDChoiceCommand& Command, ELDReceiptOutcome Outcome, ELDGrowthReason Reason)
{
    FLDChoiceReceipt Receipt;
    Receipt.Header.Generation = Command.Generation;
    Receipt.Header.Family = ELDGrowthFamily::Choice;
    Receipt.Header.RequestId = Command.RequestId;
    Receipt.Header.OriginalLife = Command.Life;
    Receipt.Header.OriginalLifePhase = Command.ExpectedLifePhase;
    Receipt.Header.Outcome = Outcome;
    Receipt.Header.Reason = LDGrowthWire::Raw(Reason);
    Receipt.OfferId = Command.OfferId;
    Receipt.OptionId = Command.OptionId;
    return Receipt;
}

struct FGrowthTestAdmission
{
    ELDAdmissionStatus Status = ELDAdmissionStatus::None;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDChoiceReceipt* Final = nullptr;
    bool bReserved = false;
};

FGrowthTestAdmission GrowthTestAdmit(TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>& Stream, const FLDChoiceCommand& Command,
    uint64 Arrival)
{
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    Info.OriginalLifePhase = Command.ExpectedLifePhase;
    FGrowthTestAdmission Result;
    Result.Status = Stream.Admit(Command, Command.RequestId, Info, Arrival, Result.Reason, Result.Final, Result.bReserved);
    return Result;
}

bool GrowthTestExpectAdmission(FAutomationTestBase& Test, const FGrowthTestAdmission& Actual, ELDAdmissionStatus Status,
    ELDGrowthReason Reason, bool bReserved, const TCHAR* What)
{
    bool bGood = GrowthTestEnum(Test, FString::Printf(TEXT("%s: admission"), What), Actual.Status, Status);
    bGood &= GrowthTestReason(Test, FString::Printf(TEXT("%s: reason"), What), Actual.Reason, Reason);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: sequence %s"), What, bReserved ? TEXT("reserved") : TEXT("not reserved")),
        Actual.bReserved == bReserved);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: a receipt is returned only for a Final admission"), What),
        (Actual.Final != nullptr) == (Status == ELDAdmissionStatus::Final));
    return bGood;
}

bool GrowthTestPendingIs(FAutomationTestBase& Test, const TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>& Stream,
    const FLDChoiceCommand& Command, uint64 Arrival, const TCHAR* What)
{
    const TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>::FPending* Pending = Stream.GetPending();
    if (!Test.TestNotNull(FString::Printf(TEXT("%s: a request is pending"), What), Pending))
    {
        return false;
    }
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s: pending request id"), What), Stream.PendingRequestId(), Command.RequestId);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: the pending terms are the admitted ones"), What), Pending->Command.SameTerms(Command));
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: pending arrival"), What), Pending->Arrival, Arrival);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: pending original life and phase"), What),
        Pending->Info.OriginalLife == Command.Life && Pending->Info.OriginalLifePhase == Command.ExpectedLifePhase);
    return bGood;
}

bool GrowthTestRunRequests(FAutomationTestBase& Test)
{
    TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt> Stream;
    const FLDChoiceReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* Info = nullptr;
    bool bGood = true;

    FGrowthTestAdmission Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(0), 100);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::InvalidRequest, false,
        TEXT("Request id 0"));
    bGood &= Test.TestNull(TEXT("Request id 0 leaves nothing pending"), Stream.GetPending());

    const FLDChoiceCommand First = GrowthTestChoiceCommand(1);
    Admission = GrowthTestAdmit(Stream, First, 101);
    if (!GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None, true, TEXT("Request 1"))
        || !GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After admitting request 1")))
    {
        return false;
    }
    Admission = GrowthTestAdmit(Stream, First, 102);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None, true,
        TEXT("Request 1 resent with the same terms"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the identical resend"));
    FLDChoiceCommand ChangedFirst = First;
    ChangedFirst.OptionId = FGuid(0x0B710000, 9, 9, 9);
    Admission = GrowthTestAdmit(Stream, ChangedFirst, 103);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict, false,
        TEXT("Request 1 resent with another option"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the conflicting resend"));
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(2), 104);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestInFlight, false,
        TEXT("Request 2 while request 1 is pending"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the in-flight refusal"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of pending request 1"), Stream.Lookup(1, Final, Info), ELDReceiptLookup::Pending);
    bGood &= Test.TestTrue(TEXT("A pending lookup reports only the original life and phase"),
        Info && !Final && Info->OriginalLife == First.Life && Info->OriginalLifePhase == First.ExpectedLifePhase);

    if (!Test.TestEqual(TEXT("Request 1 is pending before it finalizes"), Stream.PendingRequestId(), uint64(1)))
    {
        return false;
    }
    Stream.Finalize(GrowthTestChoiceReceipt(First, ELDReceiptOutcome::Rejected, ELDGrowthReason::OfferGuardStale));
    bGood &= Test.TestNull(TEXT("Finalizing request 1 clears the pending slot"), Stream.GetPending());
    bGood &= Test.TestEqual(TEXT("Request 1 is finalized"), Stream.GetFinalizedThrough(), uint64(1));
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(3), 105);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OutOfOrder, false,
        TEXT("Request 3 skipping request 2"));
    Admission = GrowthTestAdmit(Stream, First, 106);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::Final, ELDGrowthReason::None, true,
        TEXT("Replaying finalized request 1"));
    bGood &= Test.TestTrue(TEXT("The replay returns the stored rejected receipt instead of executing again"),
        Admission.Final && Admission.Final->Header.RequestId == 1 && Admission.Final->Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Admission.Final->Header.Reason) == ELDGrowthReason::OfferGuardStale
        && Admission.Final->OptionId == First.OptionId);
    bGood &= Test.TestNull(TEXT("A replay does not reopen the request"), Stream.GetPending());
    Admission = GrowthTestAdmit(Stream, ChangedFirst, 107);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict, false,
        TEXT("Finalized request 1 with another option"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of finalized request 1"), Stream.Lookup(1, Final, Info), ELDReceiptLookup::Final);
    bGood &= Test.TestTrue(TEXT("A final lookup returns the stored receipt"),
        Final && !Info && Final->Header.RequestId == 1 && Final->OfferId == First.OfferId);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 2 before it is admitted"), Stream.Lookup(2, Final, Info),
        ELDReceiptLookup::NotFound);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request id 0"), Stream.Lookup(0, Final, Info), ELDReceiptLookup::NotFound);
    bGood &= Test.TestTrue(TEXT("A NotFound lookup carries nothing"), !Final && !Info);

    const int32 Capacity = TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>::WindowCapacity;
    for (uint64 RequestId = 2; RequestId <= uint64(Capacity) + 1; ++RequestId)
    {
        const FLDChoiceCommand Ordered = GrowthTestChoiceCommand(RequestId);
        const FGrowthTestAdmission InOrder = GrowthTestAdmit(Stream, Ordered, 200 + RequestId);
        if (!Test.TestTrue(FString::Printf(TEXT("Request %llu is admitted in order"), RequestId),
            InOrder.Status == ELDAdmissionStatus::AcceptedPending && Stream.PendingRequestId() == RequestId))
        {
            return false;
        }
        Stream.Finalize(GrowthTestChoiceReceipt(Ordered, ELDReceiptOutcome::Applied, ELDGrowthReason::None));
    }
    bGood &= Test.TestEqual(TEXT("65 requests are finalized"), Stream.GetFinalizedThrough(), uint64(Capacity) + 1);
    Admission = GrowthTestAdmit(Stream, First, 300);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::ReceiptExpired, false,
        TEXT("Request 1 after 65 finalized requests"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 1 outside the 64-receipt window"), Stream.Lookup(1, Final, Info),
        ELDReceiptLookup::ReceiptExpired);
    bGood &= Test.TestTrue(TEXT("An expired lookup carries no receipt"), !Final && !Info);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 2, the oldest receipt still in the window"), Stream.Lookup(2, Final, Info),
        ELDReceiptLookup::Final);
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(2), 301);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::Final, ELDGrowthReason::None, true,
        TEXT("Replaying request 2 inside the window"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 66 before it is admitted"), Stream.Lookup(66, Final, Info),
        ELDReceiptLookup::NotFound);
    return bGood;
}

// ---- Progression rules ------------------------------------------------------------------------

// Reads the producer-exported approved_base only; there is no live-data fallback.
bool GrowthTestReadFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& OutCatalog)
{
    FString Root;
    FString Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. ")
            TEXT("No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Growth fixtures are isolated under this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    LDDemoContent::FCheck Check(Error);
    TArray<uint8> Bytes;
    LDDemoContent::FValue IndexValue;
    if (!LDDemoContent::ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
        || !LDDemoContent::Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
    {
        Test.AddError(Error);
        return false;
    }
    LDDemoContent::FFields Index(Check, IndexValue, TEXT("fixture index"));
    bool bTestOnly = false;
    const TArray<LDDemoContent::FValue>* Valid = nullptr;
    const TArray<LDDemoContent::FValue>* Invalid = nullptr;
    if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1"))
        || !Index.Flag(TEXT("test_only"), bTestOnly) || !Check.Require(bTestOnly, Index.Path, TEXT("test-only marker required"))
        || !LDDemoContent::Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
        || !LDDemoContent::Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const LDDemoContent::FValue& RowValue : *Valid)
    {
        LDDemoContent::FFields Row(Check, RowValue, TEXT("fixture case"));
        FString Name;
        if (!Row.Identifier(TEXT("name"), Name))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Name.Equals(TEXT("approved_base"), ESearchCase::CaseSensitive))
        {
            continue;
        }
        const TArray<LDDemoContent::FValue>* ChangedPaths = nullptr;
        FString MutatedQuantity;
        if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
            || !Row.Digest(TEXT("catalog_sha256"), Digest)
            || !LDDemoContent::Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, LDDemoContent::MaxFileBytes)
            || !Row.Text(TEXT("mutated_quantity"), MutatedQuantity, 0) || !Row.Done())
        {
            Test.AddError(Error);
            return false;
        }
        bFound = true;
    }
    if (!Test.TestTrue(TEXT("approved_base is explicit; synthetic base is not an approved balance fixture"), bFound))
    {
        return false;
    }
    FLDDemoContentBundle Bundle;
    ULDDemoContentCatalog* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, OutCatalog, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FLDDemoContentParameters& Tuning = OutCatalog->Get().Parameters;
    const FLDDemoProgressionDefinition& Curve = OutCatalog->Get().Progression;
    const TArray<int64> ApprovedThresholds = {0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440};
    const FLDDemoEquipmentDefinition* IronPlate = nullptr;
    FString Missing;
    bool bApproved = Test.TestTrue(TEXT("Fixture carries XP.Threshold 0/80/180/300/440/600/780/980/1200/1440"),
        Curve.XPStart == 0 && Curve.XPThresholds == ApprovedThresholds);
    bApproved &= Test.TestTrue(TEXT("Fixture carries MaxLevel 10, MaxChoices 10 (opening + nine level-ups), MaxSkillRank 2, two active and two equipment slots"),
        Curve.MaxLevel == 10 && Curve.MaxChoices == 10 && Curve.MaxSkillRank == 2 && Curve.ActiveSlots == 2 && Curve.EquipmentSlots == 2);
    bApproved &= Test.TestTrue(TEXT("Fixture carries REWARD.PlayerXP 100 + 20/level and REWARD.PlayerGold 80 + 10/level"),
        Tuning.PlayerXPBase == 100 && Tuning.PlayerXPPerLevel == 20 && Tuning.PlayerGoldBase == 80 && Tuning.PlayerGoldPerLevel == 10);
    bApproved &= Test.TestTrue(TEXT("Fixture carries SHOP.UpgradeCost 240, SHOP.StartingGold 320 and SHOP.SellRatio .65"),
        Tuning.UpgradeCost == 240 && Tuning.StartingGold == 320 && Tuning.SellRatioBp == 6500);
    bApproved &= Test.TestTrue(TEXT("Fixture prices item_iron_plate at 310 / 201 as the ledger test assumes"),
        OutCatalog->FindEquipment(TEXT("item_iron_plate"), IronPlate, Missing) && IronPlate->BuyPrice == 310 && IronPlate->SellGold == 201);
    const FLDDemoEquipmentDefinition* SiphonEmblem = nullptr;
    bApproved &= Test.TestTrue(TEXT("Fixture prices item_siphon_emblem at 360, above the starting gold, as the ledger test assumes"),
        OutCatalog->FindEquipment(TEXT("item_siphon_emblem"), SiphonEmblem, Missing) && SiphonEmblem->BuyPrice == 360);
    return bApproved;
}

FLDChoiceOption GrowthTestOption(ELDChoiceOptionKind Kind, FName DefinitionId, uint8 FromRank, uint8 ToRank)
{
    FLDChoiceOption Result;
    Result.Kind = Kind;
    Result.DefinitionId = DefinitionId;
    Result.FromRank = FromRank;
    Result.ToRank = ToRank;
    return Result;
}

bool GrowthTestPoolHas(const TArray<LDProgression::FLegalOption>& Pool, ELDChoiceOptionKind Kind, FName DefinitionId, uint8 FromRank,
    uint8 ToRank)
{
    return Pool.ContainsByPredicate([&](const LDProgression::FLegalOption& Row)
    {
        return Row.Kind == Kind && Row.DefinitionId == DefinitionId && Row.FromRank == FromRank && Row.ToRank == ToRank;
    });
}

bool GrowthTestPoolMentions(const TArray<LDProgression::FLegalOption>& Pool, FName DefinitionId)
{
    return Pool.ContainsByPredicate([&](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == DefinitionId; });
}

int32 GrowthTestKindCount(const TArray<LDProgression::FLegalOption>& Pool, ELDChoiceOptionKind Kind)
{
    int32 Count = 0;
    for (const LDProgression::FLegalOption& Row : Pool)
    {
        Count += Row.Kind == Kind ? 1 : 0;
    }
    return Count;
}

bool GrowthTestPoolSorted(const TArray<LDProgression::FLegalOption>& Pool)
{
    for (int32 Index = 1; Index < Pool.Num(); ++Index)
    {
        const LDProgression::FLegalOption& Earlier = Pool[Index - 1];
        const LDProgression::FLegalOption& Later = Pool[Index];
        const int32 Comparison = Earlier.DefinitionId.ToString().Compare(Later.DefinitionId.ToString(), ESearchCase::CaseSensitive);
        if (Comparison > 0 || (Comparison == 0 && uint8(Earlier.Kind) >= uint8(Later.Kind)))
        {
            return false;
        }
    }
    return true;
}

bool GrowthTestSameOptions(const TArray<LDProgression::FLegalOption>& A, const TArray<LDProgression::FLegalOption>& B)
{
    if (A.Num() != B.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
        if (A[Index].Kind != B[Index].Kind || A[Index].DefinitionId != B[Index].DefinitionId || A[Index].FromRank != B[Index].FromRank
            || A[Index].ToRank != B[Index].ToRank)
        {
            return false;
        }
    }
    return true;
}

bool GrowthTestBuildSorted(const FLDGrowthBuild& Build)
{
    for (int32 Index = 1; Index < Build.Skills.Num(); ++Index)
    {
        if (Build.Skills[Index - 1].Id.ToString().Compare(Build.Skills[Index].Id.ToString(), ESearchCase::CaseSensitive) >= 0)
        {
            return false;
        }
    }
    for (int32 Index = 1; Index < Build.Cards.Num(); ++Index)
    {
        if (Build.Cards[Index - 1].Id.ToString().Compare(Build.Cards[Index].Id.ToString(), ESearchCase::CaseSensitive) >= 0)
        {
            return false;
        }
    }
    return true;
}

// Only an expected success replaces Build, so an unexpected outcome cannot leak into later steps.
bool GrowthTestApply(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, FLDGrowthBuild& Build,
    const FLDChoiceOption& ChoiceOption, const LDProgression::FReplacement* Replacement, ELDGrowthReason Expected,
    LDProgression::FAppliedOption& Applied, const TCHAR* What)
{
    FLDGrowthBuild Candidate;
    const ELDGrowthReason Outcome = LDProgression::ApplyOption(Catalog, Build, ChoiceOption, Replacement, Candidate, Applied);
    if (!GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Outcome, Expected))
    {
        return false;
    }
    if (Outcome != ELDGrowthReason::None)
    {
        return true;
    }
    const bool bSorted = Test.TestTrue(FString::Printf(TEXT("%s: build rows stay sorted by id"), What), GrowthTestBuildSorted(Candidate));
    Build = MoveTemp(Candidate);
    return bSorted;
}

bool GrowthTestExpectSkill(FAutomationTestBase& Test, const FLDGrowthBuild& Build, FName SkillId, uint8 ExpectedRank, bool bActive,
    uint8 ExpectedSlot, const TCHAR* What)
{
    const FLDLearnedSkill* Learned = Build.FindSkill(SkillId);
    if (!Test.TestNotNull(FString::Printf(TEXT("%s: %s is learned"), What, *SkillId.ToString()), Learned))
    {
        return false;
    }
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s: %s rank"), What, *SkillId.ToString()), int32(Learned->Rank), int32(ExpectedRank));
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: %s is %s"), What, *SkillId.ToString(), bActive ? TEXT("active") : TEXT("passive")),
        Learned->bActive == bActive);
    if (bActive)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("%s: %s slot"), What, *SkillId.ToString()), int32(Learned->Slot), int32(ExpectedSlot));
    }
    return bGood;
}

FName GrowthTestActiveIn(const FLDGrowthBuild& Build, uint8 Slot)
{
    const int32 Index = Build.ActiveInSlot(Slot);
    return Build.Skills.IsValidIndex(Index) ? Build.Skills[Index].Id : NAME_None;
}

int32 GrowthTestCardCount(const FLDGrowthBuild& Build, FName CardId)
{
    const FLDAttributeCardCount* Card = Build.Cards.FindByPredicate([&](const FLDAttributeCardCount& Row) { return Row.Id == CardId; });
    return Card ? int32(Card->Count) : 0;
}

bool GrowthTestPlan(FAutomationTestBase& Test, const FLDDemoProgressionDefinition& Curve, int64 CurrentXP, uint8 CurrentLevel, int64 Grant,
    int32 ExpectedLevel, int32 ExpectedEarnedThrough, const TCHAR* What)
{
    LDProgression::FExperienceChange Change;
    const ELDGrowthReason Outcome = LDProgression::PlanExperience(Curve, CurrentXP, CurrentLevel, CurrentLevel, Grant, Change);
    bool bGood = GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Outcome, ELDGrowthReason::None);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: cumulative XP"), What), Change.NewXP, CurrentXP + Grant);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level before"), What), int32(Change.LevelBefore), int32(CurrentLevel));
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level after"), What), int32(Change.LevelAfter), ExpectedLevel);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: opportunities earned through"), What), int32(Change.EarnedThroughAfter),
        ExpectedEarnedThrough);
    return bGood;
}

bool GrowthTestRunProgression(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Approved = Catalog->Get();
    const FLDDemoProgressionDefinition& Curve = Approved.Progression;
    bool bGood = true;

    const TPair<int64, int32> LevelCases[] = {{0, 1}, {79, 1}, {80, 2}, {180, 3}, {1439, 9}, {1440, 10}, {MAX_int64, 10}};
    for (const TPair<int64, int32>& LevelCase : LevelCases)
    {
        uint8 Level = 0;
        const ELDGrowthReason Outcome = LDProgression::LevelForXP(Curve, LevelCase.Key, Level);
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("LevelForXP(%lld) outcome"), LevelCase.Key), Outcome, ELDGrowthReason::None);
        bGood &= Test.TestEqual(FString::Printf(TEXT("Level at %lld cumulative XP"), LevelCase.Key), int32(Level), LevelCase.Value);
    }
    uint8 RefusedLevel = 5;
    bGood &= GrowthTestReason(Test, TEXT("LevelForXP(-1)"), LDProgression::LevelForXP(Curve, -1, RefusedLevel),
        ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("A refused LevelForXP reports no level"), int32(RefusedLevel), 0);

    // The opening opportunity is ordinal 1, so a contender holds as many opportunities as its level.
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 79, 1, 1, TEXT("79 XP at level 1 keeps only the opening opportunity"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 80, 2, 2, TEXT("80 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 1440, 10, 10, TEXT("1440 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 1440, 10, 1000, 10, 10, TEXT("1000 XP at level 10"));
    LDProgression::FExperienceChange Change;
    bGood &= GrowthTestReason(Test, TEXT("1 XP past the int64 bound"), LDProgression::PlanExperience(Curve, MAX_int64, 10, 10, 1, Change),
        ELDGrowthReason::ArithmeticOverflow);
    bGood &= Test.TestTrue(TEXT("An overflowing plan carries no partial result"), Change.NewXP == 0 && Change.LevelAfter == 0);
    bGood &= GrowthTestReason(Test, TEXT("80 XP recorded as level 1"), LDProgression::PlanExperience(Curve, 80, 1, 1, 20, Change),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 1 without its opening opportunity"),
        LDProgression::PlanExperience(Curve, 0, 1, 0, 80, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 2 without its earned opportunity"),
        LDProgression::PlanExperience(Curve, 80, 2, 1, 20, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("A negative XP grant"), LDProgression::PlanExperience(Curve, 0, 1, 1, -1, Change),
        ELDGrowthReason::DataInvalid);

    const uint8 MaxLevel = uint8(Curve.MaxLevel);
    LDProgression::FRewardAmounts Reward;
    bGood &= GrowthTestReason(Test, TEXT("A level 1 contender kill"),
        LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, 1, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("Level 1 contender kill XP"), Reward.XP, int64(100));
    bGood &= Test.TestEqual(TEXT("Level 1 contender kill gold"), Reward.Gold, int64(80));
    bGood &= GrowthTestReason(Test, TEXT("A level 10 contender kill"),
        LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, 10, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("Level 10 contender kill XP"), Reward.XP, int64(280));
    bGood &= Test.TestEqual(TEXT("Level 10 contender kill gold"), Reward.Gold, int64(170));
    for (const uint8 VictimLevel : {uint8(0), uint8(11)})
    {
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("A level %d contender kill"), VictimLevel),
            LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, VictimLevel, Reward), ELDGrowthReason::DataInvalid);
        bGood &= Test.TestTrue(FString::Printf(TEXT("A refused level %d kill grants nothing"), VictimLevel),
            Reward.XP == 0 && Reward.Gold == 0);
    }
    bGood &= GrowthTestReason(Test, TEXT("A monster_scavenger kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_scavenger")), Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("monster_scavenger XP"), Reward.XP, int64(40));
    bGood &= Test.TestEqual(TEXT("monster_scavenger gold"), Reward.Gold, int64(30));
    bGood &= GrowthTestReason(Test, TEXT("A monster_guardian kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_guardian")), Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("monster_guardian XP"), Reward.XP, int64(160));
    bGood &= Test.TestEqual(TEXT("monster_guardian gold"), Reward.Gold, int64(120));
    bGood &= GrowthTestReason(Test, TEXT("An unknown monster kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_unknown")), Reward), ELDGrowthReason::DataInvalid);

    FLDGrowthBuild EmptyBuild;
    TArray<LDProgression::FLegalOption> Pool;
    if (!GrowthTestReason(Test, TEXT("The legal pool of an empty level 1 build"), LDProgression::LegalPool(*Catalog, EmptyBuild, Pool),
        ELDGrowthReason::None))
    {
        return false;
    }
    // The empty build may take every catalog definition once: each active, each passive and each attribute card.
    // The expected pool is read from the approved catalog, so new content needs no edit here.
    TArray<TPair<ELDChoiceOptionKind, FString>> ApprovedPool;
    int32 Actives = 0, Passives = 0;
    for (const FLDDemoAbilityDefinition& Ability : Approved.Abilities)
    {
        const bool bActive = Ability.Kind == ELDDemoAbilityKind::Active;
        (bActive ? Actives : Passives) += 1;
        ApprovedPool.Emplace(bActive ? ELDChoiceOptionKind::LearnActive : ELDChoiceOptionKind::LearnPassive, Ability.Id);
    }
    for (const FLDDemoAttributeDefinition& Attribute : Approved.Attributes)
    {
        ApprovedPool.Emplace(ELDChoiceOptionKind::Attribute, Attribute.Id);
    }
    ApprovedPool.Sort([](const TPair<ELDChoiceOptionKind, FString>& A, const TPair<ELDChoiceOptionKind, FString>& B)
        { return A.Value.Compare(B.Value, ESearchCase::CaseSensitive) < 0; });
    bGood &= Test.TestTrue(TEXT("The approved catalog has actives, passives and attribute cards"),
        Actives > 0 && Passives > 0 && Approved.Attributes.Num() > 0);
    bGood &= Test.TestEqual(TEXT("An empty build may learn every active"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnActive), Actives);
    bGood &= Test.TestEqual(TEXT("An empty build may learn every passive"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnPassive), Passives);
    bGood &= Test.TestEqual(TEXT("An empty build may take every attribute card"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::Attribute), Approved.Attributes.Num());
    if (!Test.TestEqual(TEXT("The empty build's legal pool holds every catalog definition once"), Pool.Num(), ApprovedPool.Num()))
    {
        return false;
    }
    TSet<FName> PoolIds;
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        const LDProgression::FLegalOption& Row = Pool[Index];
        PoolIds.Add(Row.DefinitionId);
        bGood &= Test.TestTrue(FString::Printf(TEXT("Pool row %d is %s from rank 0 to 1"), Index, *ApprovedPool[Index].Value),
            Row.Kind == ApprovedPool[Index].Key && Row.DefinitionId.ToString().Equals(ApprovedPool[Index].Value, ESearchCase::CaseSensitive)
            && Row.FromRank == 0 && Row.ToRank == 1);
    }
    bGood &= Test.TestTrue(TEXT("The pool is sorted by definition id, then kind"), GrowthTestPoolSorted(Pool));
    bGood &= Test.TestEqual(TEXT("No definition appears twice in the pool"), PoolIds.Num(), Pool.Num());

    const FGuid MatchSecret(0x5EC2E700, 1, 2, 3);
    const FGuid Contender(0x0000A001, 0, 0, 1);
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, Contender, 1);
    bGood &= Test.TestTrue(TEXT("Retrying opportunity 1 reuses its seed"), Seed == LDProgression::OfferSeed(MatchSecret, Contender, 1));
    bGood &= Test.TestTrue(TEXT("Opportunity 2 draws from another seed"), Seed != LDProgression::OfferSeed(MatchSecret, Contender, 2));
    bGood &= Test.TestTrue(TEXT("Another contender's opportunity 1 draws from another seed"),
        Seed != LDProgression::OfferSeed(MatchSecret, FGuid(0x0000A002, 0, 0, 2), 1));
    bGood &= Test.TestTrue(TEXT("The reroll of an opportunity draws from its own seed"),
        LDProgression::RerollSeed(MatchSecret, Contender, 1) != Seed
        && LDProgression::RerollSeed(MatchSecret, Contender, 1) == LDProgression::RerollSeed(MatchSecret, Contender, 1));
    // A level-up draw over the empty build's pool (112 §6 composition is covered by DemoGrowthDraft).
    LDProgression::FDrawInput Input;
    Input.Content = &Catalog->Get();
    Input.Build = &EmptyBuild;
    Input.Ordinal = 2;
    Input.Seed = Seed;
    TArray<LDProgression::FLegalOption> Offer;
    if (!GrowthTestReason(Test, TEXT("Drawing an offer"), LDProgression::DrawOffer(Input, Pool, Offer), ELDGrowthReason::None)
        || !Test.TestEqual(TEXT("An offer holds three options"), Offer.Num(), 3))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The three offered options name distinct definitions"), Offer[0].DefinitionId != Offer[1].DefinitionId
        && Offer[0].DefinitionId != Offer[2].DefinitionId && Offer[1].DefinitionId != Offer[2].DefinitionId);
    for (const LDProgression::FLegalOption& Offered : Offer)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("Offered %s comes from the legal pool"), *Offered.DefinitionId.ToString()),
            GrowthTestPoolHas(Pool, Offered.Kind, Offered.DefinitionId, Offered.FromRank, Offered.ToRank));
    }
    TArray<LDProgression::FLegalOption> Redrawn;
    bGood &= GrowthTestReason(Test, TEXT("Redrawing with the same seed"), LDProgression::DrawOffer(Input, Pool, Redrawn),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The same seed draws the same options in the same order"), GrowthTestSameOptions(Offer, Redrawn));
    TSet<FName> Reachable;
    int32 DraftableSkills = 0;
    for (const FLDDemoAbilityDefinition& Ability : Approved.Abilities)
    {
        DraftableSkills += Ability.Draftable ? 1 : 0;
    }
    for (uint64 TrialSeed = 1; TrialSeed <= 256; ++TrialSeed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = TrialSeed;
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("Drawing with seed %llu"), TrialSeed),
            LDProgression::DrawOffer(Input, Pool, Drawn), ELDGrowthReason::None);
        for (const LDProgression::FLegalOption& DrawnOption : Drawn)
        {
            Reachable.Add(DrawnOption.DefinitionId);
        }
    }
    bGood &= Test.TestEqual(TEXT("Every draftable skill and attribute card is drawn by some of 256 seeds"), Reachable.Num(),
        DraftableSkills + Approved.Attributes.Num());
    TArray<LDProgression::FLegalOption> Refused;
    Input.Ordinal = 0;
    bGood &= GrowthTestReason(Test, TEXT("Opportunity 0"), LDProgression::DrawOffer(Input, Pool, Refused), ELDGrowthReason::DataInvalid);
    Input.Ordinal = 2;
    const TArray<LDProgression::FLegalOption> TwoRows = {Pool[0], Pool[1]};
    bGood &= GrowthTestReason(Test, TEXT("Drawing three from two legal options"), LDProgression::DrawOffer(Input, TwoRows, Refused),
        ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("Refused draws offer nothing"), Refused.Num(), 0);

    const FName SnarkBolt(TEXT("active_snark_bolt"));
    const FName PantsDown(TEXT("active_pants_down"));
    const FName NotMad(TEXT("active_not_mad"));
    FLDGrowthBuild Build;
    LDProgression::FAppliedOption Applied;

    const FLDChoiceOption LearnSnark = GrowthTestOption(ELDChoiceOptionKind::LearnActive, SnarkBolt, 0, 1);
    bGood &= Test.TestTrue(TEXT("Learning a first active is legal on an empty build"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnSnark));
    if (!GrowthTestApply(Test, *Catalog, Build, LearnSnark, nullptr, ELDGrowthReason::None, Applied, TEXT("Learning the first active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, SnarkBolt, 1, true, 0, TEXT("The first active"));
    bGood &= Test.TestTrue(TEXT("The first active lands in Q (slot 0) at rank 1 and replaces nothing"), Applied.Slot == 0
        && !Applied.bReplaced && Applied.NewRank == 1 && Applied.Kind == ELDChoiceOptionKind::LearnActive
        && Applied.DefinitionId == SnarkBolt);
    bGood &= Test.TestFalse(TEXT("The applied learn option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnSnark));
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnSnark, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Applying the first active again"));

    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnActive, PantsDown, 0, 1), nullptr,
        ELDGrowthReason::None, Applied, TEXT("Learning the second active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 1, true, 1, TEXT("The second active"));
    bGood &= Test.TestEqual(TEXT("The second active lands in W (slot 1)"), int32(Applied.Slot), 1);

    const FLDChoiceOption UpgradeSnark = GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, SnarkBolt, 1, 2);
    if (!GrowthTestApply(Test, *Catalog, Build, UpgradeSnark, nullptr, ELDGrowthReason::None, Applied, TEXT("Upgrading Q to rank 2")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, SnarkBolt, 2, true, 0, TEXT("The upgraded Q active"));
    bGood &= Test.TestFalse(TEXT("The applied upgrade option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, UpgradeSnark));

    const FLDChoiceOption LearnNotMad = GrowthTestOption(ELDChoiceOptionKind::LearnActive, NotMad, 0, 1);
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnNotMad, nullptr, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("A third active without a replacement"));
    LDProgression::FReplacement Replacement;
    Replacement.Slot = 0;
    Replacement.OldSkillId = SnarkBolt;
    Replacement.OldRank = 1;
    Replacement.bConfirmed = true;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnNotMad, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming rank 1 of the rank 2 Q active"));
    Replacement.OldRank = 2;
    Replacement.bConfirmed = false;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnNotMad, &Replacement, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("An unconfirmed replacement"));
    Replacement.bConfirmed = true;
    Replacement.Slot = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnNotMad, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming the wrong slot"));
    Replacement.Slot = 0;
    if (!GrowthTestApply(Test, *Catalog, Build, LearnNotMad, &Replacement, ELDGrowthReason::None, Applied,
        TEXT("The confirmed replacement of Q")))
    {
        return false;
    }
    bGood &= Test.TestNull(TEXT("The replaced active and its rank are forgotten"), Build.FindSkill(SnarkBolt));
    bGood &= GrowthTestExpectSkill(Test, Build, NotMad, 1, true, 0, TEXT("The replacing active"));
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 1, true, 1, TEXT("The untouched W active"));
    bGood &= Test.TestTrue(TEXT("The replacement reports the slot and the forgotten skill and rank"), Applied.bReplaced && Applied.Slot == 0
        && Applied.ReplacedId == SnarkBolt && Applied.ReplacedRank == 2 && Applied.NewRank == 1 && Applied.DefinitionId == NotMad);
    TArray<LDProgression::FLegalOption> AfterReplacement;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after the replacement"),
        LDProgression::LegalPool(*Catalog, Build, AfterReplacement), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("A forgotten active can be learned again from rank 1"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::LearnActive, SnarkBolt, 0, 1));
    bGood &= Test.TestTrue(TEXT("Held actives offer their next rank"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2)
        && GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, NotMad, 1, 2));
    bGood &= Test.TestTrue(TEXT("The pool stays sorted after a replacement"), GrowthTestPoolSorted(AfterReplacement));

    // A replacement that would be valid for Q is still refused by every option that is not a slot-bound new active.
    Replacement.OldSkillId = NotMad;
    Replacement.OldRank = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build,
        GrowthTestOption(ELDChoiceOptionKind::LearnPassive, FName(TEXT("passive_lucky_star")), 0, 1), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to a passive"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, FName(TEXT("attr_protein")), 0, 1),
        &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an attribute card"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an upgrade"));
    FLDGrowthBuild FreeSlots;
    bGood &= GrowthTestApply(Test, *Catalog, FreeSlots, LearnSnark, &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied,
        TEXT("A replacement while an active slot is free"));

    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2), nullptr,
        ELDGrowthReason::None, Applied, TEXT("Upgrading W to rank 2")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("An upgrade keeps the active in its slot"), int32(Applied.Slot), 1);
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 2, true, 1, TEXT("The rank 2 W active"));
    TArray<LDProgression::FLegalOption> MaxedPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool with a rank 2 active"), LDProgression::LegalPool(*Catalog, Build, MaxedPool),
        ELDGrowthReason::None);
    bGood &= Test.TestFalse(TEXT("A rank 2 (advanced) active is absent from the legal pool"), GrowthTestPoolMentions(MaxedPool, PantsDown));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 2, 3), nullptr,
        ELDGrowthReason::InvalidOption, Applied, TEXT("Upgrading past rank 2"));

    for (const TCHAR* PassiveText : {TEXT("passive_iced_americano"), TEXT("passive_lucky_star"), TEXT("passive_freeloader")})
    {
        const FName PassiveId(PassiveText);
        if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnPassive, PassiveId, 0, 1), nullptr,
            ELDGrowthReason::None, Applied, PassiveText))
        {
            return false;
        }
        bGood &= GrowthTestExpectSkill(Test, Build, PassiveId, 1, false, 0, TEXT("A passive learned with both active slots held"));
    }
    bGood &= Test.TestEqual(TEXT("Q still holds active_not_mad after three passives"), GrowthTestActiveIn(Build, 0), NotMad);
    bGood &= Test.TestEqual(TEXT("W still holds active_pants_down after three passives"), GrowthTestActiveIn(Build, 1), PantsDown);
    bGood &= Test.TestEqual(TEXT("Two actives and three passives are learned"), Build.Skills.Num(), 5);

    const FName Protein(TEXT("attr_protein"));
    const FLDChoiceOption FirstProtein = GrowthTestOption(ELDChoiceOptionKind::Attribute, Protein, 0, 1);
    if (!GrowthTestApply(Test, *Catalog, Build, FirstProtein, nullptr, ELDGrowthReason::None, Applied, TEXT("The first attr_protein card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_protein count after one card"), GrowthTestCardCount(Build, Protein), 1);
    bGood &= Test.TestEqual(TEXT("The first card reports its new count"), int32(Applied.NewRank), 1);
    bGood &= Test.TestFalse(TEXT("The applied card option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, FirstProtein));
    TArray<LDProgression::FLegalOption> CardPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after one card"), LDProgression::LegalPool(*Catalog, Build, CardPool),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The pool offers attr_protein from count 1 to 2"),
        GrowthTestPoolHas(CardPool, ELDChoiceOptionKind::Attribute, Protein, 1, 2));
    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, Protein, 1, 2), nullptr,
        ELDGrowthReason::None, Applied, TEXT("The second attr_protein card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_protein count after two cards"), GrowthTestCardCount(Build, Protein), 2);
    bGood &= Test.TestEqual(TEXT("The second card reports its new count"), int32(Applied.NewRank), 2);
    bGood &= GrowthTestApply(Test, *Catalog, Build, FirstProtein, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Re-applying the stale first card"));
    bGood &= Test.TestEqual(TEXT("Other attribute cards are untouched"), GrowthTestCardCount(Build, FName(TEXT("attr_canteen"))), 0);
    bGood &= Test.TestTrue(TEXT("ApplyOption leaves the level and build revision to its caller"), Build.Level == 1 && Build.Revision == 1);
    return bGood;
}

// ---- DEV growth fixture ------------------------------------------------------------------------

struct FGrowthTestPreset
{
    const TCHAR* What;
    TArray<FName> Cards;
};

// Off without -LDDevGrowthFixture; a preset resolves only to three distinct legal cards of the build; each preset shapes
// exactly one head draw and the next head is the ordinary seeded draw again.
bool GrowthTestRunDevFixture(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    FString Error;
    bool bGood = Test.TestFalse(TEXT("The rules run carries no -LDDevGrowthFixture switch"), LDDevGrowthFixture::IsSwitchPresent());
    bGood &= Test.TestTrue(TEXT("Without the switch the fixture admits no caller and says why"),
        !LDDevGrowthFixture::Admit(nullptr, Error) && Error.Contains(TEXT("-LDDevGrowthFixture")));
    {
        const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!Test.TestNotNull(TEXT("An isolated world for the fixture entries"), World))
        {
            return false;
        }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
        ALDEconomyDirector* Economy = World->SpawnActor<ALDEconomyDirector>();
        if (!Test.TestNotNull(TEXT("A growth director"), Development) || !Test.TestNotNull(TEXT("An economy director"), Economy))
        {
            return false;
        }
        const FGuid Contender(0xE3, 0, 0, 1);
        FString QueueError;
        FString GrantError;
        FString Detail;
        bGood &= Test.TestTrue(TEXT("DevQueueOffer is refused without the switch and names it"),
            !Development->DevQueueOffer(Contender, {FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
                FName(TEXT("attr_all_nighter"))}, QueueError) && QueueError.Contains(TEXT("-LDDevGrowthFixture")));
        FString StageError;
        bGood &= Test.TestTrue(TEXT("StageOffer (the Lobby entry) is refused without the switch and names it"),
            !LDDevGrowthFixture::StageOffer(World, Contender, {FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
                FName(TEXT("attr_all_nighter"))}, StageError) && StageError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("DevGrantReward is refused without the switch and issues no ticket"),
            Economy->DevGrantReward(Contender, 80, 50, GrantError) == 0 && GrantError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("No ticket exists to poll after a refused grant"),
            Economy->DevGrantStatus(1, Detail) == ELDDevGrantStatus::Unknown);
    }

    const FName Snark(TEXT("active_snark_bolt"));
    const FName PantsDown(TEXT("active_pants_down"));
    const FName NotMad(TEXT("active_not_mad"));
    const FName LuckyStar(TEXT("passive_lucky_star"));
    const FName Protein(TEXT("attr_protein"));
    const FName AllNighter(TEXT("attr_all_nighter"));
    const FName HandOut(TEXT("attr_hand_out"));
    FLDGrowthBuild Fresh;
    TArray<LDProgression::FLegalOption> Resolved;
    bGood &= Test.TestTrue(FString::Printf(TEXT("Three legal cards resolve in their given order with kinds from the build (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Fresh, {Snark, LuckyStar, Protein}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].DefinitionId == Snark && Resolved[0].Kind == ELDChoiceOptionKind::LearnActive && Resolved[0].ToRank == 1
        && Resolved[1].DefinitionId == LuckyStar && Resolved[1].Kind == ELDChoiceOptionKind::LearnPassive
        && Resolved[2].DefinitionId == Protein && Resolved[2].Kind == ELDChoiceOptionKind::Attribute && Resolved[2].ToRank == 1);
    FLDGrowthBuild Learned;
    Learned.Level = 6;
    Learned.Skills = {{Snark, 1, true, 0}, {NotMad, 2, true, 1}};
    Learned.Cards = {{Protein, 2}};
    bGood &= Test.TestTrue(FString::Printf(TEXT("A learned skill resolves to its next rank and a held card to one more (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, {Snark, Protein, PantsDown}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].Kind == ELDChoiceOptionKind::UpgradeActive && Resolved[0].FromRank == 1 && Resolved[0].ToRank == 2
        && Resolved[1].Kind == ELDChoiceOptionKind::Attribute && Resolved[1].FromRank == 2 && Resolved[1].ToRank == 3
        && Resolved[2].Kind == ELDChoiceOptionKind::LearnActive);
    const FGrowthTestPreset BadPresets[] = {
        {TEXT("two cards"), {Snark, Protein}},
        {TEXT("four cards"), {Snark, Protein, AllNighter, HandOut}},
        {TEXT("a repeated card"), {Snark, Protein, Snark}},
        {TEXT("an unknown definition"), {Snark, Protein, FName(TEXT("active_unknown"))}},
        {TEXT("NAME_None"), {Snark, Protein, FName()}},
        {TEXT("a skill at its rank cap"), {NotMad, Protein, AllNighter}}};
    for (const FGrowthTestPreset& Bad : BadPresets)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("A preset with %s is refused with a reason and no cards"), Bad.What),
            !LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, Bad.Cards, Resolved, Error) && Resolved.IsEmpty() && !Error.IsEmpty());
    }

    TArray<LDProgression::FLegalOption> Pool;
    TArray<LDProgression::FLegalOption> Ordinary;
    LDProgression::FDrawInput Input;
    Input.Content = &Catalog->Get();
    Input.Build = &Fresh;
    Input.Ordinal = 2;
    Input.Seed = LDProgression::OfferSeed(FGuid(1, 2, 3, 4), FGuid(0xE3, 0, 0, 1), 1);
    bGood &= GrowthTestReason(Test, TEXT("The fresh build's legal pool"), LDProgression::LegalPool(*Catalog, Fresh, Pool), ELDGrowthReason::None);
    bGood &= GrowthTestReason(Test, TEXT("The ordinary seeded draw"), LDProgression::DrawOffer(Input, Pool, Ordinary),
        ELDGrowthReason::None);
    TArray<TArray<FName>> Queue = {{PantsDown, AllNighter, HandOut}, {Snark, FName(TEXT("active_unknown")), Protein},
        {NotMad, LuckyStar, Protein}};
    TArray<LDProgression::FLegalOption> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("The first head draw takes the oldest preset, in its order"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && PresetError.IsEmpty() && Picked.Num() == 3 && Picked[0].DefinitionId == PantsDown && Picked[1].DefinitionId == AllNighter
        && Picked[2].DefinitionId == HandOut && Queue.Num() == 2);
    bGood &= Test.TestTrue(TEXT("A preset that no longer resolves is consumed with an explicit error and the ordinary draw runs"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && !PresetError.IsEmpty() && GrowthTestSameOptions(Picked, Ordinary) && Queue.Num() == 1);
    bGood &= Test.TestTrue(TEXT("The next head draw takes the next preset"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && Picked[0].DefinitionId == NotMad && Picked[1].DefinitionId == LuckyStar && Picked[2].DefinitionId == Protein && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("With no preset left the head is the ordinary seeded draw: a preset shapes one offer only"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && PresetError.IsEmpty() && GrowthTestSameOptions(Picked, Ordinary));
    return bGood;
}

// ---- Private owner view schema ----------------------------------------------------------------

void GrowthTestReachableStructs(const FProperty* Property, TSet<const UScriptStruct*>& Out)
{
    if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
    {
        bool bSeen = false;
        Out.Add(AsStruct->Struct, &bSeen);
        if (!bSeen)
        {
            for (TFieldIterator<FProperty> It(AsStruct->Struct); It; ++It)
            {
                GrowthTestReachableStructs(*It, Out);
            }
        }
    }
    else if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Property))
    {
        GrowthTestReachableStructs(AsArray->Inner, Out);
    }
    else if (const FSetProperty* AsSet = CastField<FSetProperty>(Property))
    {
        GrowthTestReachableStructs(AsSet->ElementProp, Out);
    }
    else if (const FMapProperty* AsMap = CastField<FMapProperty>(Property))
    {
        GrowthTestReachableStructs(AsMap->KeyProp, Out);
        GrowthTestReachableStructs(AsMap->ValueProp, Out);
    }
}

bool GrowthTestCarriesReference(const UStruct* Struct)
{
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        const FProperty* Leaf = *It;
        if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Leaf))
        {
            Leaf = AsArray->Inner;
        }
        if (CastField<FObjectPropertyBase>(Leaf) || CastField<FInterfaceProperty>(Leaf) || CastField<FDelegateProperty>(Leaf)
            || CastField<FMulticastDelegateProperty>(Leaf) || CastField<FSetProperty>(Leaf) || CastField<FMapProperty>(Leaf))
        {
            return true;
        }
        if (const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf); AsStruct && GrowthTestCarriesReference(AsStruct->Struct))
        {
            return true;
        }
    }
    return false;
}

bool GrowthTestExactFields(FAutomationTestBase& Test, const UScriptStruct* Struct, std::initializer_list<const TCHAR*> Names)
{
    TSet<FName> Expected;
    for (const TCHAR* Name : Names)
    {
        Expected.Add(FName(Name));
    }
    TSet<FName> Actual;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        Actual.Add(It->GetFName());
    }
    FString Extra;
    for (const FName& Name : Actual.Difference(Expected))
    {
        Extra += Name.ToString() + TEXT(" ");
    }
    FString Missing;
    for (const FName& Name : Expected.Difference(Actual))
    {
        Missing += Name.ToString() + TEXT(" ");
    }
    return Test.TestTrue(FString::Printf(TEXT("%s has exactly its approved owner fields (extra: %s; missing: %s)"),
        *Struct->GetName(), *Extra, *Missing), Extra.IsEmpty() && Missing.IsEmpty());
}

// D10: BeginMatch hands every roster contender, human or AI, exactly one opening opportunity with its kept first offer.
// Only a level crossed adds another (a death or respawn has no growth entry), and a new match starts over at one.
bool GrowthTestRunOpening(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!Test.TestNotNull(TEXT("An isolated world for the growth director"), World))
    {
        return false;
    }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Test.TestNotNull(TEXT("A growth director"), Development))
    {
        return false;
    }
    const FGuid Human(0xD10, 0, 0, 1);
    const FGuid Bot(0xD10, 0, 0, 2);
    const FGuid Late(0xD10, 0, 0, 3);
    FLDMatchContext First;
    First.Generation = 1;
    First.LedgerMatch = FGuid(0xD10, 1, 0, 0);
    FString Error;
    if (!Test.TestTrue(FString::Printf(TEXT("The first match begins (%s)"), *Error), Development->BeginMatch(First, {Human, Bot}, Catalog, Error)))
    {
        return false;
    }
    TArray<LDProgression::FLegalOption> Pool;
    bool bGood = GrowthTestReason(Test, TEXT("The starting build's legal pool"), LDProgression::LegalPool(*Catalog, FLDGrowthBuild(), Pool),
        ELDGrowthReason::None);
    const auto CheckOpening = [&Test, &Pool](const FLDProgressionView& View, uint32 Generation, const FString& Who)
    {
        bool bOpening = Test.TestTrue(Who + TEXT(": level 1 at the opening XP holds exactly one unconsumed opportunity"),
            View.bValid && View.Generation == Generation && View.Level == 1 && View.CumulativeXP == 0 && View.EarnedThrough == 1
            && View.ConsumedThrough == 0 && View.PendingChoiceCount == 1);
        bOpening &= Test.TestTrue(Who + TEXT(": the opening offer is kept as opportunity 1 with three distinct cards"),
            View.bHasOffer && View.Offer.OpportunityOrdinal == 1 && View.Offer.OfferId.IsValid() && View.Offer.Options.Num() == 3
            && View.Offer.Options[0].DefinitionId != View.Offer.Options[1].DefinitionId
            && View.Offer.Options[0].DefinitionId != View.Offer.Options[2].DefinitionId
            && View.Offer.Options[1].DefinitionId != View.Offer.Options[2].DefinitionId);
        for (const FLDChoiceOption& Option : View.Offer.Options)
        {
            bOpening &= Test.TestTrue(FString::Printf(TEXT("%s: opening card %s is legal for the starting build"), *Who,
                *Option.DefinitionId.ToString()), GrowthTestPoolHas(Pool, Option.Kind, Option.DefinitionId, Option.FromRank, Option.ToRank));
        }
        return bOpening;
    };
    TMap<FGuid, FGuid> FirstOffers;
    for (const FGuid& Id : {Human, Bot})
    {
        const FString Who = Id == Human ? TEXT("Human") : TEXT("AI");
        const FLDProgressionView View = Development->ReadProgression(Id);
        bGood &= CheckOpening(View, First.Generation, Who);
        FirstOffers.Add(Id, View.Offer.OfferId);
        const FLDProgressionView Again = Development->ReadProgression(Id);
        bGood &= Test.TestTrue(Who + TEXT(": a later read (as after a death or respawn) neither grants nor redraws"),
            Again.EarnedThrough == 1 && Again.PendingChoiceCount == 1 && Again.Offer.OfferId == View.Offer.OfferId
            && Again.Offer.OfferRevision == View.Offer.OfferRevision && Again.ProgressionRevision == View.ProgressionRevision);
    }
    bGood &= Test.TestNotEqual(TEXT("Each contender draws its own opening offer"), FirstOffers[Human], FirstOffers[Bot]);

    // XP short of level 2 adds nothing; each level crossed adds exactly one behind the kept opening offer, ten in all.
    const FLDPrepareGrowthResult Short = Development->PrepareExperience(Human, 79);
    bGood &= Test.TestTrue(TEXT("79 XP keeps only the opening opportunity and its offer"), Short.IsSuccess()
        && Short.Value->Build().Level == 1 && Short.Value->Progression().EarnedThrough == 1
        && Short.Value->Progression().Offer.IsSet() && Short.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Up = Development->PrepareExperience(Human, 80);
    bGood &= Test.TestTrue(TEXT("Level 2 adds opportunity 2 behind the unchanged opening offer"), Up.IsSuccess()
        && Up.Value->Build().Level == 2 && Up.Value->Progression().EarnedThrough == 2 && Up.Value->Progression().PendingCount() == 2
        && Up.Value->Progression().Offer.IsSet() && Up.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Top = Development->PrepareExperience(Human, 1440);
    bGood &= Test.TestTrue(TEXT("Level 10 holds MaxChoices opportunities: the opening one and nine level-ups"), Top.IsSuccess()
        && Top.Value->Build().Level == 10 && Top.Value->Progression().EarnedThrough == 10
        && int32(Top.Value->Progression().EarnedThrough) == Catalog->Get().Progression.MaxChoices);

    FLDMatchContext Second;
    Second.Generation = 2;
    Second.LedgerMatch = FGuid(0xD10, 2, 0, 0);
    if (!Test.TestTrue(FString::Printf(TEXT("A new match begins (%s)"), *Error), Development->BeginMatch(Second, {Human, Late}, Catalog, Error)))
    {
        return false;
    }
    const FLDProgressionView Again = Development->ReadProgression(Human);
    bGood &= CheckOpening(Again, Second.Generation, TEXT("Human in the new match"));
    bGood &= Test.TestNotEqual(TEXT("The new match draws a new opening offer"), Again.Offer.OfferId, FirstOffers[Human]);
    bGood &= CheckOpening(Development->ReadProgression(Late), Second.Generation, TEXT("A contender new to the second match"));
    bGood &= Test.TestFalse(TEXT("A contender outside the new roster has no growth record"), Development->ReadProgression(Bot).bValid);
    return bGood;
}

// 112 §6: the kept opening offer holds OpeningActiveCount new draftable actives and OpeningPassiveCount new draftable
// passives; a skill marked non-draftable never appears in any offer. A short group is refused, never filled.
bool GrowthTestRunOpeningPool(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    bool bGood = Test.TestTrue(TEXT("The approved opening offer is two new actives plus one new passive"),
        Progression.OpeningActiveCount == 2 && Progression.OpeningPassiveCount == 1);
    // The draftable sets are read, not listed.
    TSet<FName> Actives, Passives;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        if (Ability.Draftable)
        {
            (Ability.Kind == ELDDemoAbilityKind::Active ? Actives : Passives).Add(FName(*Ability.Id));
        }
    }
    TArray<LDProgression::FLegalOption> Pool;
    if (!GrowthTestReason(Test, TEXT("The starting build's legal pool"), LDProgression::LegalPool(*Catalog, FLDGrowthBuild(), Pool),
        ELDGrowthReason::None))
    {
        return false;
    }
    const auto CheckComposition = [&](const FString& Who, const TArray<FName>& Ids, const TArray<ELDChoiceOptionKind>& Kinds)
    {
        int32 ActiveCards = 0;
        int32 PassiveCards = 0;
        bool bLegal = Ids.Num() == 3 && Kinds.Num() == 3 && Ids[0] != Ids[1] && Ids[0] != Ids[2] && Ids[1] != Ids[2];
        for (int32 Index = 0; bLegal && Index < 3; ++Index)
        {
            if (Kinds[Index] == ELDChoiceOptionKind::LearnActive && Actives.Contains(Ids[Index])) { ++ActiveCards; }
            else if (Kinds[Index] == ELDChoiceOptionKind::LearnPassive && Passives.Contains(Ids[Index])) { ++PassiveCards; }
            else { bLegal = false; }
        }
        return Test.TestTrue(Who + TEXT(": three distinct new draftable skills, two actives and one passive"),
            bLegal && ActiveCards == Progression.OpeningActiveCount && PassiveCards == Progression.OpeningPassiveCount);
    };
    const FLDGrowthBuild Empty;
    LDProgression::FDrawInput Input;
    Input.Content = &Content;
    Input.Build = &Empty;
    Input.Ordinal = 1;
    TSet<FName> Reached;
    int32 PassivePositions[3] = {0, 0, 0};
    for (uint64 Seed = 1; Seed <= 512; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = Seed;
        const ELDGrowthReason Reason = LDProgression::DrawOffer(Input, Pool, Drawn);
        TArray<FName> Ids;
        TArray<ELDChoiceOptionKind> Kinds;
        for (int32 Index = 0; Index < Drawn.Num(); ++Index)
        {
            Ids.Add(Drawn[Index].DefinitionId);
            Kinds.Add(Drawn[Index].Kind);
            Reached.Add(Drawn[Index].DefinitionId);
            PassivePositions[Index] += Drawn[Index].Kind == ELDChoiceOptionKind::LearnPassive ? 1 : 0;
            bGood &= GrowthTestPoolHas(Pool, Drawn[Index].Kind, Drawn[Index].DefinitionId, Drawn[Index].FromRank, Drawn[Index].ToRank);
        }
        if (Reason != ELDGrowthReason::None || !CheckComposition(FString::Printf(TEXT("Opening seed %llu"), Seed), Ids, Kinds))
        {
            return false;
        }
    }
    bGood &= Test.TestEqual(TEXT("512 opening seeds reach every draftable active and passive, nothing else"),
        Reached.Num(), Actives.Num() + Passives.Num());
    bGood &= Test.TestTrue(TEXT("The passive card appears in every position"),
        PassivePositions[0] > 0 && PassivePositions[1] > 0 && PassivePositions[2] > 0);
    TArray<LDProgression::FLegalOption> First, Again;
    Input.Seed = 77;
    bGood &= GrowthTestReason(Test, TEXT("An opening draw"), LDProgression::DrawOffer(Input, Pool, First), ELDGrowthReason::None);
    bGood &= GrowthTestReason(Test, TEXT("The same opening seed"), LDProgression::DrawOffer(Input, Pool, Again), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The same seed redraws the same opening offer"), GrowthTestSameOptions(First, Again));

    // Contract 2: a non-draftable skill (new actives until #117) never enters any offer, opening or level-up.
    FLDDemoContentDefinitions Gated = Content;
    const FName Held(TEXT("active_snark_bolt"));
    Gated.Abilities.FindByPredicate([](const FLDDemoAbilityDefinition& Row)
        { return Row.Id == TEXT("active_snark_bolt"); })->Draftable = false;
    LDProgression::FDrawInput GatedInput = Input;
    GatedInput.Content = &Gated;
    FLDGrowthBuild Level3;
    Level3.Level = 3;
    Level3.Skills = {{FName(TEXT("passive_lucky_star")), 1, false, 0}};
    bool bNeverGated = true;
    for (uint64 Seed = 1; Seed <= 512; ++Seed)
    {
        for (const uint8 Ordinal : {uint8(1), uint8(3)})
        {
            TArray<LDProgression::FLegalOption> LevelPool, Drawn;
            const FLDGrowthBuild& Build = Ordinal == 1 ? Empty : Level3;
            LDProgression::LegalPool(*Catalog, Build, LevelPool);
            GatedInput.Build = &Build;
            GatedInput.Ordinal = Ordinal;
            GatedInput.Seed = Seed;
            bNeverGated &= LDProgression::DrawOffer(GatedInput, LevelPool, Drawn) == ELDGrowthReason::None
                && !Drawn.ContainsByPredicate([Held](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == Held; });
        }
    }
    bGood &= Test.TestTrue(TEXT("A non-draftable active never appears in an opening or level-up offer"), bNeverGated);
    // Still legal to learn outside a draw (DEV learn), so it stays in the legal pool.
    bGood &= Test.TestTrue(TEXT("The non-draftable active stays in the legal pool"), GrowthTestPoolHas(Pool, ELDChoiceOptionKind::LearnActive, Held, 0, 1));

    // Refusals: a short group, malformed counts and opportunity 0 offer nothing.
    TArray<LDProgression::FLegalOption> Refused;
    FLDDemoContentDefinitions Short = Content;
    for (FLDDemoAbilityDefinition& Ability : Short.Abilities)
    {
        Ability.Draftable = Ability.Kind == ELDDemoAbilityKind::Passive || Ability.Id == TEXT("active_snark_bolt");
    }
    LDProgression::FDrawInput ShortInput = Input;
    ShortInput.Content = &Short;
    bGood &= Test.TestFalse(TEXT("One draftable active cannot fill two active cards"), LDProgression::DrawFeasible(Short, Empty, Pool, 1));
    bGood &= GrowthTestReason(Test, TEXT("Drawing an opening with one draftable active"), LDProgression::DrawOffer(ShortInput, Pool, Refused),
        ELDGrowthReason::DataInvalid);
    FLDDemoContentDefinitions Four = Content;
    Four.Progression.OpeningPassiveCount = 2;
    ShortInput.Content = &Four;
    bGood &= GrowthTestReason(Test, TEXT("An opening of four cards"), LDProgression::DrawOffer(ShortInput, Pool, Refused),
        ELDGrowthReason::DataInvalid);
    ShortInput.Content = &Content;
    ShortInput.Ordinal = 0;
    bGood &= GrowthTestReason(Test, TEXT("Opportunity 0"), LDProgression::DrawOffer(ShortInput, Pool, Refused), ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("Refused opening draws offer nothing"), Refused.Num(), 0);

    // The DEV fixture still presets any legal cards for the opening; without a preset it draws the opening composition.
    TArray<TArray<FName>> Queue = {{FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
        FName(TEXT("attr_all_nighter"))}};
    TArray<LDProgression::FLegalOption> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("An opening preset is still honoured as the DEV override"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None
        && bPreset && Picked.Num() == 3 && Picked[0].DefinitionId == FName(TEXT("active_snark_bolt")) && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("Without a preset the fixture draws the ordinary opening offer"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None
        && !bPreset && GrowthTestSameOptions(Picked, First));

    // Every roster contender's kept opening offer follows the composition.
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!Test.TestNotNull(TEXT("An isolated world for the growth director"), World))
    {
        return false;
    }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Test.TestNotNull(TEXT("A growth director"), Development))
    {
        return false;
    }
    TArray<FGuid> Roster;
    for (uint32 Index = 1; Index <= 6; ++Index)
    {
        Roster.Add(FGuid(0xD24, 0, 0, Index));
    }
    FLDMatchContext Match;
    Match.Generation = 1;
    Match.LedgerMatch = FGuid(0xD24, 1, 0, 0);
    FString Error;
    if (!Test.TestTrue(FString::Printf(TEXT("The match begins (%s)"), *Error), Development->BeginMatch(Match, Roster, Catalog, Error)))
    {
        return false;
    }
    for (const FGuid& Id : Roster)
    {
        const FLDProgressionView View = Development->ReadProgression(Id);
        TArray<FName> Ids;
        TArray<ELDChoiceOptionKind> Kinds;
        for (const FLDChoiceOption& Option : View.Offer.Options)
        {
            Ids.Add(Option.DefinitionId);
            Kinds.Add(Option.Kind);
        }
        bGood &= Test.TestTrue(TEXT("The kept offer is opportunity 1"), View.bHasOffer && View.Offer.OpportunityOrdinal == 1)
            && CheckComposition(TEXT("Roster contender ") + Id.ToString(), Ids, Kinds);
    }
    return bGood;
}

bool GrowthTestRunPrivacy(FAutomationTestBase& Test)
{
    // Any field added to the private growth views must be added here deliberately (and told to UI).
    bool bGood = GrowthTestExactFields(Test, FLDGrowthOwnerView::StaticStruct(),
        {TEXT("Generation"), TEXT("PublishRevision"), TEXT("Progression"), TEXT("Equipment"), TEXT("Shop"),
            TEXT("AirdropClaimedSerial")});
    bGood &= GrowthTestExactFields(Test, FLDProgressionView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Life"), TEXT("LifePhase"), TEXT("bCanChoose"), TEXT("Level"), TEXT("MaxLevel"),
         TEXT("CumulativeXP"), TEXT("LevelThresholdXP"), TEXT("bHasNextThreshold"), TEXT("NextThresholdXP"),
         TEXT("ProgressionRevision"), TEXT("BuildRevision"), TEXT("EarnedThrough"), TEXT("ConsumedThrough"),
         TEXT("PendingChoiceCount"), TEXT("RerollTokens"), TEXT("bHasOffer"), TEXT("Offer"), TEXT("Skills"), TEXT("AttributeCards"),
         TEXT("CatalogDigest")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOfferView::StaticStruct(),
        {TEXT("OfferId"), TEXT("OpportunityOrdinal"), TEXT("OfferRevision"), TEXT("GuardBuildRevision"), TEXT("bRerolled"), TEXT("Options")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOption::StaticStruct(),
        {TEXT("OptionId"), TEXT("Kind"), TEXT("DefinitionId"), TEXT("FromRank"), TEXT("ToRank")});
    bGood &= GrowthTestExactFields(Test, FLDLearnedSkillView::StaticStruct(),
        {TEXT("DefinitionId"), TEXT("bActive"), TEXT("Rank"), TEXT("Slot")});
    bGood &= GrowthTestExactFields(Test, FLDAttributeCardView::StaticStruct(), {TEXT("DefinitionId"), TEXT("Count")});
    bGood &= GrowthTestExactFields(Test, FLDEquipmentView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Gold"), TEXT("AccountRevision"), TEXT("InventoryRevision"), TEXT("bUpgradeUsed"),
         TEXT("Slots")});
    bGood &= GrowthTestExactFields(Test, FLDEquipmentSlotView::StaticStruct(),
        {TEXT("SlotIndex"), TEXT("bOccupied"), TEXT("Instance"), TEXT("IssueSerial"), TEXT("DefinitionId"), TEXT("ItemRevision"),
         TEXT("bUpgraded")});
    bGood &= GrowthTestExactFields(Test, FLDShopOpenResult::StaticStruct(),
        {TEXT("Generation"), TEXT("Life"), TEXT("ShopId"), TEXT("OpenStatus"), TEXT("Reason"), TEXT("bHasView"), TEXT("View")});
    bGood &= GrowthTestExactFields(Test, FLDShopView::StaticStruct(),
        {TEXT("ShopId"), TEXT("SessionId"), TEXT("Life"), TEXT("QuoteRevision"), TEXT("CatalogDigest"), TEXT("WorldMapId"),
         TEXT("WorldLayoutRevision"), TEXT("WorldGeometryRevision"), TEXT("Items"), TEXT("Sellable"), TEXT("UpgradePrice"),
         TEXT("bUpgradeUsed")});
    bGood &= GrowthTestExactFields(Test, FLDShopItemView::StaticStruct(),
        {TEXT("DefinitionId"), TEXT("BuyPrice"), TEXT("SellGold")});
    bGood &= GrowthTestExactFields(Test, FLDShopSellView::StaticStruct(),
        {TEXT("SlotIndex"), TEXT("Instance"), TEXT("ItemRevision"), TEXT("DefinitionId"), TEXT("SellPrice"), TEXT("bUpgraded"),
         TEXT("bCanUpgrade")});
    bGood &= Test.TestFalse(TEXT("The owner growth view carries no object, delegate or unordered container"),
        GrowthTestCarriesReference(FLDGrowthOwnerView::StaticStruct()));

    const TSet<const UScriptStruct*> GrowthPrivateViews = {FLDGrowthOwnerView::StaticStruct(), FLDProgressionView::StaticStruct(),
        FLDChoiceOfferView::StaticStruct(), FLDChoiceOption::StaticStruct(), FLDLearnedSkillView::StaticStruct(),
        FLDAttributeCardView::StaticStruct(), FLDEquipmentView::StaticStruct(), FLDEquipmentSlotView::StaticStruct(),
        FLDShopOpenResult::StaticStruct(), FLDShopView::StaticStruct(), FLDShopItemView::StaticStruct(), FLDShopSellView::StaticStruct()};
    TSet<const UScriptStruct*> Nested;
    for (TFieldIterator<FProperty> It(FLDGrowthOwnerView::StaticStruct()); It; ++It)
    {
        GrowthTestReachableStructs(*It, Nested);
    }
    Nested = Nested.Difference(GrowthPrivateViews);
    Nested.Remove(TBaseStructure<FGuid>::Get());
    Nested.Remove(FLDDemoContentDigest::StaticStruct());
    bGood &= Test.TestEqual(TEXT("The owner growth view nests only its approved structs, FGuid and the catalog digest"), Nested.Num(), 0);

    int32 GrowthMembers = 0;
    bool bOnlyGrowth = false;
    for (TFieldIterator<FProperty> It(FLDOwnerState::StaticStruct()); It; ++It)
    {
        TSet<const UScriptStruct*> Reached;
        GrowthTestReachableStructs(*It, Reached);
        if (Reached.Intersect(GrowthPrivateViews).Num() > 0)
        {
            ++GrowthMembers;
            const FStructProperty* AsStruct = CastField<FStructProperty>(*It);
            bOnlyGrowth = It->GetFName() == FName(TEXT("Growth")) && AsStruct && AsStruct->Struct == FLDGrowthOwnerView::StaticStruct();
        }
    }
    bGood &= Test.TestEqual(TEXT("FLDOwnerState carries growth through exactly one member"), GrowthMembers, 1);
    bGood &= Test.TestTrue(TEXT("That member is FLDOwnerState.Growth"), bOnlyGrowth);
    bGood &= Test.TestTrue(TEXT("The owner state replicates only through the owning controller"),
        GetDefault<ALDPlayerController>()->bOnlyRelevantToOwner);

    for (const UClass* PublicClass : {ALDGameState::StaticClass(), ALDPlayerState::StaticClass(), ALDCharacter::StaticClass()})
    {
        for (TFieldIterator<FProperty> It(PublicClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            TSet<const UScriptStruct*> Reached;
            GrowthTestReachableStructs(*It, Reached);
            bGood &= Test.TestEqual(FString::Printf(TEXT("%s.%s exposes no private growth view"), *PublicClass->GetName(), *It->GetName()),
                Reached.Intersect(GrowthPrivateViews).Num(), 0);
        }
    }
    TSet<const UScriptStruct*> Observed;
    for (TFieldIterator<FProperty> It(FLDWorldView::StaticStruct()); It; ++It)
    {
        GrowthTestReachableStructs(*It, Observed);
    }
    bGood &= Test.TestEqual(TEXT("Observed world views expose no private growth view"), Observed.Intersect(GrowthPrivateViews).Num(), 0);
    return bGood;
}
// ---- #147 slotted draft: upgrade / new skill / free slot, the full-slot active weight, reroll avoidance, replacement ----
FLDGrowthBuild GrowthTestDraftBuild(uint8 Level, std::initializer_list<FLDLearnedSkill> Skills)
{
    FLDGrowthBuild Build;
    Build.Level = Level;
    Build.Skills = Skills;
    Build.Skills.Sort([](const FLDLearnedSkill& A, const FLDLearnedSkill& B)
        { return A.Id.ToString().Compare(B.Id.ToString(), ESearchCase::CaseSensitive) < 0; });
    return Build;
}

// Draftable skills of the approved catalog in catalog order, so the draft rules are checked on whatever it holds.
void GrowthTestDraftable(const FLDDemoContentDefinitions& Content, TArray<FName>& Actives, TArray<FName>& Passives)
{
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        if (Ability.Draftable)
        {
            (Ability.Kind == ELDDemoAbilityKind::Active ? Actives : Passives).Add(FName(*Ability.Id));
        }
    }
}

bool GrowthTestRunDraft(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoChoiceWeights& W = Content.Parameters.Choice;
    const uint8 TopRank = uint8(Content.Progression.MaxSkillRank);
    bool bGood = Test.TestTrue(TEXT("The approved draft: a new active weighs 0.5 with Q/W full, the free slot is an attribute 40%"),
        W.NewActiveFullSlotWeightBp == 5000 && W.FreeSlotAttributeBp == 4000);
    bGood &= Test.TestTrue(TEXT("Rerolls are earned at levels 4 and 7"), Content.Progression.RerollLevels == TArray<int32>{4, 7});
    const uint8 ExpectedTokens[] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 2};
    for (uint8 Level = 1; Level <= 10; ++Level)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Rerolls earned by level %u"), uint32(Level)),
            LDProgression::RerollTokensEarned(Content.Progression, Level), ExpectedTokens[Level - 1]);
    }
    TArray<FName> Actives, Passives;
    GrowthTestDraftable(Content, Actives, Passives);
    if (!Test.TestTrue(TEXT("The catalog drafts at least four actives and three passives"), Actives.Num() >= 4 && Passives.Num() >= 3))
    {
        return false;
    }
    const auto Weight = [&](const FLDGrowthBuild& Build, ELDChoiceOptionKind Kind, FName Id)
    {
        int64 Out = -1;
        return LDProgression::NewSkillWeight(Content, Build, {Kind, Id, 0, 1}, Out) == ELDGrowthReason::None ? Out : int64(-1);
    };
    const FLDGrowthBuild Empty;
    const FLDGrowthBuild Full = GrowthTestDraftBuild(4, {{Actives[0], 1, true, 0}, {Actives[1], 1, true, 1}, {Passives[0], 1, false, 0}});
    bGood &= Test.TestEqual(TEXT("A new active with a free Q/W weighs 1"), Weight(Empty, ELDChoiceOptionKind::LearnActive, Actives[2]), int64(10000));
    bGood &= Test.TestEqual(TEXT("A new passive weighs 1"), Weight(Full, ELDChoiceOptionKind::LearnPassive, Passives[1]), int64(10000));
    bGood &= Test.TestEqual(TEXT("A new active with Q/W full weighs 0.5"), Weight(Full, ELDChoiceOptionKind::LearnActive, Actives[2]), int64(5000));
    int64 Refused = 0;
    bGood &= GrowthTestReason(Test, TEXT("An upgrade has no new-skill weight"),
        LDProgression::NewSkillWeight(Content, Full, {ELDChoiceOptionKind::UpgradePassive, Passives[0], 1, 2}, Refused), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("An attribute card has no new-skill weight"),
        LDProgression::NewSkillWeight(Content, Full, {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1}, Refused),
        ELDGrowthReason::DataInvalid);

    // Slots over many seeds: an upgrade first, a new skill second, the free slot an attribute about 40% of the time;
    // with Q/W full at most one new active; three distinct cards; new skills drawn without regard to the build.
    TArray<LDProgression::FLegalOption> FullPool;
    bGood &= GrowthTestReason(Test, TEXT("The full build's pool"), LDProgression::LegalPool(*Catalog, Full, FullPool), ELDGrowthReason::None);
    LDProgression::FDrawInput Input;
    Input.Content = &Content;
    Input.Build = &Full;
    Input.Ordinal = 4;
    int32 FreeAttributes = 0, Violations = 0;
    TMap<FName, int32> SecondSlot;
    constexpr int32 Seeds = 4096;
    for (int32 Seed = 1; Seed <= Seeds; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = uint64(Seed) * 7919;
        if (LDProgression::DrawOffer(Input, FullPool, Drawn) != ELDGrowthReason::None || Drawn.Num() != 3)
        {
            ++Violations;
            continue;
        }
        const bool bUpgrade = Drawn[0].Kind == ELDChoiceOptionKind::UpgradeActive || Drawn[0].Kind == ELDChoiceOptionKind::UpgradePassive;
        const bool bNew = Drawn[1].Kind == ELDChoiceOptionKind::LearnActive || Drawn[1].Kind == ELDChoiceOptionKind::LearnPassive;
        const bool bFreeOk = Drawn[2].Kind == ELDChoiceOptionKind::Attribute || Drawn[2].Kind == ELDChoiceOptionKind::LearnActive
            || Drawn[2].Kind == ELDChoiceOptionKind::LearnPassive;
        const int32 NewActives = Drawn.FilterByPredicate([](const LDProgression::FLegalOption& Row)
            { return Row.Kind == ELDChoiceOptionKind::LearnActive; }).Num();
        FreeAttributes += Drawn[2].Kind == ELDChoiceOptionKind::Attribute ? 1 : 0;
        ++SecondSlot.FindOrAdd(Drawn[1].DefinitionId);
        Violations += !bUpgrade || !bNew || !bFreeOk || NewActives > 1 || Drawn[0].DefinitionId == Drawn[1].DefinitionId
            || Drawn[0].DefinitionId == Drawn[2].DefinitionId || Drawn[1].DefinitionId == Drawn[2].DefinitionId ? 1 : 0;
    }
    bGood &= Test.TestEqual(TEXT("Every Q/W-full offer keeps its slots and limits"), Violations, 0);
    bGood &= Test.TestTrue(FString::Printf(TEXT("The free slot is an attribute about 40%% of the time (%d of %d)"), FreeAttributes, Seeds),
        FreeAttributes > Seeds * 35 / 100 && FreeAttributes < Seeds * 45 / 100);
    // Expected second-slot share: weight 1 per unlearned passive, 0.5 per unlearned active.
    double PassiveCount = 0, ActiveCount = 0;
    int32 PassiveRows = 0, ActiveRows = 0;
    for (const FName Id : Passives) { if (!Full.FindSkill(Id)) { PassiveCount += SecondSlot.FindRef(Id); ++PassiveRows; } }
    for (const FName Id : Actives) { if (!Full.FindSkill(Id)) { ActiveCount += SecondSlot.FindRef(Id); ++ActiveRows; } }
    const double Ratio = ActiveRows && PassiveRows && PassiveCount > 0 ? (ActiveCount / ActiveRows) / (PassiveCount / PassiveRows) : 0.;
    bGood &= Test.TestTrue(FString::Printf(TEXT("A new active shows half as often as a new passive with Q/W full (%.3f)"), Ratio),
        Ratio > 0.40 && Ratio < 0.60);
    int32 Rarest = MAX_int32, Commonest = 0;
    for (const FName Id : Passives)
    {
        if (Full.FindSkill(Id)) { continue; }
        Rarest = FMath::Min(Rarest, SecondSlot.FindRef(Id));
        Commonest = FMath::Max(Commonest, SecondSlot.FindRef(Id));
    }
    bGood &= Test.TestTrue(FString::Printf(TEXT("Every new passive is equally likely, whatever the build holds (%d..%d)"), Rarest, Commonest),
        Rarest > 0 && Commonest < Rarest * 3 / 2);

    // No learned skill below the top rank: the upgrade slot takes a new skill. Everything at the top and learned: three cards.
    FLDGrowthBuild Topped = GrowthTestDraftBuild(5, {{Actives[0], TopRank, true, 0}, {Passives[0], TopRank, false, 0}});
    TArray<LDProgression::FLegalOption> ToppedPool;
    LDProgression::LegalPool(*Catalog, Topped, ToppedPool);
    Input.Build = &Topped;
    int32 ToppedBad = 0, OpenTwoNewActives = 0;
    for (int32 Seed = 1; Seed <= 512; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = uint64(Seed);
        ToppedBad += LDProgression::DrawOffer(Input, ToppedPool, Drawn) != ELDGrowthReason::None
            || (Drawn[0].Kind != ELDChoiceOptionKind::LearnActive && Drawn[0].Kind != ELDChoiceOptionKind::LearnPassive) ? 1 : 0;
        OpenTwoNewActives += Drawn.FilterByPredicate([](const LDProgression::FLegalOption& Row)
            { return Row.Kind == ELDChoiceOptionKind::LearnActive; }).Num() >= 2 ? 1 : 0;
    }
    bGood &= Test.TestEqual(TEXT("Without an upgrade the first slot holds a new skill"), ToppedBad, 0);
    bGood &= Test.TestTrue(TEXT("With a free Q/W slot an offer may hold two new actives"), OpenTwoNewActives > 0);
    const TArray<LDProgression::FLegalOption> Cards = {
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[1].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[2].Id), 0, 1}};
    TArray<LDProgression::FLegalOption> CardsOnly;
    bGood &= GrowthTestReason(Test, TEXT("A pool of attribute cards alone"), LDProgression::DrawOffer(Input, Cards, CardsOnly),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("It still fills all three slots"), CardsOnly.Num() == 3);
    bGood &= Test.TestFalse(TEXT("Two attribute cards and nothing else cannot fill an offer"),
        LDProgression::DrawFeasible(Content, Topped, {Cards[0], Cards[1]}, 4));

    // Reroll: the same slots, the previous three cards avoided while the pool allows.
    Input.Build = &Full;
    Input.Seed = 41;
    TArray<LDProgression::FLegalOption> First;
    LDProgression::DrawOffer(Input, FullPool, First);
    int32 Overlaps = 0;
    for (int32 Seed = 1; Seed <= 256; ++Seed)
    {
        LDProgression::FDrawInput Reroll = Input;
        Reroll.Seed = LDProgression::RerollSeed(FGuid(1, 2, 3, 4), FGuid(5, 6, 7, uint32(Seed)), 4);
        Reroll.Avoid = First;
        TArray<LDProgression::FLegalOption> Again;
        if (LDProgression::DrawOffer(Reroll, FullPool, Again) != ELDGrowthReason::None) { ++Overlaps; continue; }
        for (const LDProgression::FLegalOption& Row : Again)
        {
            // The full build has three upgrades, so the upgrade slot can always avoid; so can every other slot.
            Overlaps += First.ContainsByPredicate([&Row](const LDProgression::FLegalOption& Old)
                { return Old.Kind == Row.Kind && Old.DefinitionId == Row.DefinitionId; }) ? 1 : 0;
        }
    }
    bGood &= Test.TestEqual(TEXT("A reroll never repeats a card of the previous offer while others remain"), Overlaps, 0);
    // A pool with nothing else still fills the offer: avoidance is best effort, never a refusal.
    const TArray<LDProgression::FLegalOption> Tight = {
        {ELDChoiceOptionKind::UpgradeActive, Actives[0], 1, 2}, {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[1].Id), 0, 1}};
    LDProgression::FDrawInput TightInput = Input;
    TightInput.Avoid = Tight;
    TArray<LDProgression::FLegalOption> TightDraw;
    bGood &= GrowthTestReason(Test, TEXT("A reroll of an exhausted pool"), LDProgression::DrawOffer(TightInput, Tight, TightDraw),
        ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("It repeats the only cards there are"), TightDraw.Num(), 3);

    // Replacement (#147 D13): the new active starts at rank 1 whatever the forgotten rank was.
    FLDGrowthBuild Build = GrowthTestDraftBuild(6, {{Actives[0], TopRank, true, 0}, {Actives[1], 1, true, 1}});
    LDProgression::FReplacement Replacement;
    Replacement.Slot = 0;
    Replacement.OldSkillId = Actives[0];
    Replacement.OldRank = TopRank;
    Replacement.bConfirmed = true;
    LDProgression::FAppliedOption Applied;
    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnActive, Actives[2], 0, 1), &Replacement,
        ELDGrowthReason::None, Applied, TEXT("Replacing a top-rank Q")))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("Replacing the top rank learns the new active at rank 1 in the same slot"), Applied.bReplaced
        && Applied.NewRank == 1 && Applied.ReplacedRank == TopRank && !Build.FindSkill(Actives[0]));
    bGood &= GrowthTestExpectSkill(Test, Build, Actives[2], 1, true, 0, TEXT("The replacement"));
    return bGood;
}

// ---- #147 skill growth: G(L) on a learned skill's fixed amounts, never on mana, ratios, durations or equipment ----
bool GrowthTestRunSkillGrowth(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const int64 Step = Content.Parameters.SkillGrowthPerLevelBp;
    bool bGood = Test.TestEqual(TEXT("The approved growth is 10% of the base per level"), Step, int64(1000));
    bGood &= Test.TestTrue(TEXT("G(1) = 1.0, G(5) = 1.4, G(10) = 1.9; no level 0"), LDSkillGrowth::FactorBp(Step, 1) == 10000
        && LDSkillGrowth::FactorBp(Step, 5) == 14000 && LDSkillGrowth::FactorBp(Step, 10) == 19000 && LDSkillGrowth::FactorBp(Step, 0) == 0);
    int64 Grown = 0;
    bGood &= Test.TestTrue(TEXT("A grown amount floors to P2"), LDSkillGrowth::GrowP2(6001, 14000, Grown) && Grown == 8401);
    bGood &= Test.TestFalse(TEXT("Overflow is refused"), LDSkillGrowth::GrowP2(MAX_int64 / 2, 19000, Grown));

    FLDDemoEffectDefinition Damage;
    Damage.Opcode = uint8(ELDDemoEffectOp::Damage);
    Damage.BaseP2 = 6000; Damage.ADRatioBp = 5000; Damage.DurationMs = 1000; Damage.RatioBp = 2500;
    FLDDemoEffectDefinition Mana;
    Mana.Opcode = uint8(ELDDemoEffectOp::ManaOnHit);
    Mana.BaseP2 = 1200;
    FLDDemoEffectDefinition Timed;
    Timed.Opcode = uint8(ELDDemoEffectOp::TimedStatBonus);
    Timed.DurationMs = 3000;
    Timed.Stats = {{ELDDemoBonusStat::ArmorFlat, 1200, {}}, {ELDDemoBonusStat::AttackSpeedBonusRatio, 3500, {}}};
    FLDDemoRankDefinition Rank;
    Rank.Rank = 1; Rank.ManaCostP2 = 6000; Rank.CooldownMs = 12000;
    Rank.Effects = {Damage, Mana, Timed};
    FLDDemoRankDefinition At5;
    if (!Test.TestTrue(TEXT("A rank grows at level 5"), LDSkillGrowth::GrowRank(Rank, Step, 5, At5)))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The base amount grows 1.4x; coefficients, ratios and durations do not"),
        At5.Effects[0].BaseP2 == 8400 && At5.Effects[0].ADRatioBp == 5000 && At5.Effects[0].DurationMs == 1000 && At5.Effects[0].RatioBp == 2500);
    bGood &= Test.TestEqual(TEXT("A mana grant does not grow"), At5.Effects[1].BaseP2, int64(1200));
    bGood &= Test.TestTrue(TEXT("A timed bonus grows its points, not its ratios or duration"), At5.Effects[2].Stats[0].Amount == 1680
        && At5.Effects[2].Stats[1].Amount == 3500 && At5.Effects[2].DurationMs == 3000);
    bGood &= Test.TestTrue(TEXT("Mana cost and cooldown never grow"), At5.ManaCostP2 == 6000 && At5.CooldownMs == 12000 && At5.Rank == 1);

    // The snapshot serves every authored rank for every level, grown by the approved step, and refuses anything else.
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        for (const FLDDemoRankDefinition& Authored : Ability.Ranks)
        {
            for (int32 Level = 1; Level <= Content.Progression.MaxLevel; ++Level)
            {
                const FLDDemoRankDefinition* Served = Catalog->FindRank(Ability, uint8(Authored.Rank), uint8(Level));
                FLDDemoRankDefinition Expected;
                const bool bExpected = LDSkillGrowth::GrowRank(Authored, Step, uint8(Level), Expected);
                bool bSame = Served && bExpected && Served->Effects.Num() == Expected.Effects.Num();
                for (int32 Index = 0; bSame && Index < Expected.Effects.Num(); ++Index)
                {
                    bSame = Served->Effects[Index].BaseP2 == Expected.Effects[Index].BaseP2;
                }
                if (!bSame)
                {
                    bGood &= Test.TestTrue(FString::Printf(TEXT("%s rank %d at level %d is served grown"), *Ability.Id, Authored.Rank, Level), false);
                }
            }
        }
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no level 0 row"), Catalog->FindRank(Ability, 1, 0));
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no row past the top level"), Catalog->FindRank(Ability, 1, uint8(Content.Progression.MaxLevel + 1)));
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no rank past the top"), Catalog->FindRank(Ability, uint8(Content.Progression.MaxSkillRank + 1), 1));
    }
    FLDDemoAbilityDefinition Stranger;
    Stranger.Ranks = {Rank};
    bGood &= Test.TestNull(TEXT("An ability outside the snapshot is refused"), Catalog->FindRank(Stranger, 1, 1));
    return bGood;
}

// ---- 112 §7 equipment upgrade: ledger cause, effective definitions and the attribute projection ----
bool GrowthTestRunUpgrade(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    bool bGood = Test.TestTrue(TEXT("The approved upgrade costs 240 and adds 35% to fixed stats"),
        Content.Parameters.UpgradeCost == 240 && Content.Parameters.UpgradeStatBonusBp == 3500);
    const FLDDemoEquipmentDefinition* PureStats = nullptr;
    for (const FLDDemoEquipmentDefinition& Item : Content.Equipment)
    {
        bGood &= Test.TestEqual(Item.Id + TEXT(" has one upgraded value per stat"), Item.UpgradedStats.Num(), Item.Stats.Num());
        for (int32 Index = 0; Index < Item.Stats.Num() && Index < Item.UpgradedStats.Num(); ++Index)
        {
            const FLDDemoStatBonus& Base = Item.Stats[Index];
            const FLDDemoStatBonus& Up = Item.UpgradedStats[Index];
            const bool bRange = Base.Stat == ELDDemoBonusStat::AttackRangeCmFlat;
            const int64 Expected = bRange ? Base.Cm.Numerator * 13500 / (Base.Cm.Denominator * 10000) : Base.Amount * 13500 / 10000;
            bGood &= Test.TestTrue(FString::Printf(TEXT("%s stat %d is +35%% floored at its precision"), *Item.Id, Index),
                Up.Stat == Base.Stat && (bRange ? Up.Cm.Numerator == Expected && Up.Cm.Denominator == 1 : Up.Amount == Expected));
        }
        bGood &= Test.TestTrue(Item.Id + TEXT(" is upgradable exactly when its passive, if any, has its upgraded form"),
            LDDemoEquipment::Upgradable(Item) == (!Item.HasPassive || Item.Passive.HasUpgraded));
        bGood &= Test.TestTrue(Item.Id + TEXT(" serves its base or upgraded terms, never a fallback"),
            &LDDemoEquipment::EffectiveStats(Item, false) == &Item.Stats && &LDDemoEquipment::EffectiveStats(Item, true) == &Item.UpgradedStats
            && (Item.HasPassive ? LDDemoEquipment::EffectivePassiveEffects(Item, false) == &Item.Passive.Effects
                && (LDDemoEquipment::EffectivePassiveEffects(Item, true) != nullptr) == Item.Passive.HasUpgraded
                : LDDemoEquipment::EffectivePassiveEffects(Item, true) == nullptr));
        if (!PureStats && !Item.HasPassive) { PureStats = &Item; }
    }

    // Projection: an upgraded instance projects its upgraded stats; an item without approved upgraded terms refuses.
    for (const FLDDemoEquipmentDefinition& Item : Content.Equipment)
    {
        FLDAttributeBuildValues Plain(1, {}, {}, {Item.Id});
        FLDAttributeBuildValues Upgraded(1, {}, {}, {Item.Id});
        Upgraded.EquippedUpgraded = {true};
        const FLDAttributeProjectionResult Base = LDAttributeProjection::Project(Plain, *Catalog);
        const FLDAttributeProjectionResult Up = LDAttributeProjection::Project(Upgraded, *Catalog);
        if (!LDDemoEquipment::Upgradable(Item))
        {
            bGood &= Test.TestTrue(Item.Id + TEXT(" flagged upgraded without approved terms is refused"), !Up.IsSuccess()
                && Up.Error.Code == ELDAttributeProjectionError::Catalog);
            continue;
        }
        if (!Test.TestTrue(Item.Id + TEXT(" projects plain and upgraded"), Base.IsSuccess() && Up.IsSuccess())) { bGood = false; continue; }
        for (const FLDDemoStatBonus& Stat : Item.UpgradedStats)
        {
            const FLDDemoStatBonus* Plain0 = Item.Stats.FindByPredicate([&Stat](const FLDDemoStatBonus& S) { return S.Stat == Stat.Stat; });
            if (Stat.Stat == ELDDemoBonusStat::ADFlat)
            {
                bGood &= Test.TestEqual(Item.Id + TEXT(" upgraded AD"), Up.Value->Final.ADP2 - Base.Value->Final.ADP2, Stat.Amount - Plain0->Amount);
            }
            if (Stat.Stat == ELDDemoBonusStat::MaxHPFlat)
            {
                bGood &= Test.TestEqual(Item.Id + TEXT(" upgraded max HP"), Up.Value->Final.MaxHPP2 - Base.Value->Final.MaxHPP2,
                    Stat.Amount - Plain0->Amount);
            }
        }
        if (Item.HasPassive)
        {
            // Two copies, the second upgraded: the family applies once and takes the upgraded terms.
            FLDAttributeBuildValues Pair(1, {}, {}, {Item.Id, Item.Id});
            Pair.EquippedUpgraded = {false, true};
            const FLDAttributeProjectionResult Both = LDAttributeProjection::Project(Pair, *Catalog);
            bGood &= Test.TestTrue(Item.Id + TEXT(": duplicate copies apply the family once, the upgraded copy first"), Both.IsSuccess()
                && Both.Value->EquipmentPassives.Num() == 1 && Both.Value->EquipmentPassives[0].bUpgraded
                && Both.Value->EquipmentPassives[0].EquippedIndices.Num() == 2);
        }
    }
    FLDAttributeBuildValues Mismatched(1, {}, {}, {Content.Equipment[0].Id});
    Mismatched.EquippedUpgraded = {true, false};
    bGood &= Test.TestFalse(TEXT("Mismatched upgrade flags are refused"), LDAttributeProjection::Project(Mismatched, *Catalog).IsSuccess());
    if (!Test.TestNotNull(TEXT("The catalog holds an item with fixed stats only"), PureStats)) { return false; }

    // Ledger: Burn + Upgrade, once per account, bound to the instance.
    const FGuid Match(0x112, 7, 0, 1), P1(0x112, 7, 0, 2), P2(0x112, 7, 0, 3);
    LDEconomy::FLedger Ledger;
    FString Error;
    if (!Test.TestTrue(TEXT("A ledger opens"), Ledger.Initialize(Match, {P1, P2}, 2000, Error))) { return false; }
    uint64 Batch = 1;
    const auto Buy = [&](const FGuid& Owner, uint8 Slot, int64 Price)
    {
        LDEconomy::FTransaction T;
        T.Cause = LDEconomy::ECause::Buy;
        T.BusinessKey = FGuid::NewGuid();
        T.CommitBatch = ++Batch;
        T.Burn = Price;
        T.Gold.Emplace(LDEconomy::FGoldChange{Owner, Ledger.Account(Owner)->Revision, -Price});
        T.Create.Emplace(LDEconomy::FCreateItem{Owner, Ledger.InventoryRevision(Owner), FName(*PureStats->Id), Slot});
        LDEconomy::FPrepareResult Prepared = Ledger.Prepare(T);
        if (!Prepared.IsSuccess()) { return FGuid(); }
        const FGuid Created = Prepared.Value->Entry().CreatedInstance;
        Ledger.ApplyPrepared(MoveTemp(Prepared.Value.GetValue()));
        return Created;
    };
    const auto Upgrade = [&](const FGuid& Owner, const FGuid& Instance, int64 Cost, int64 MintInstead = 0)
    {
        LDEconomy::FTransaction T;
        T.Cause = LDEconomy::ECause::Upgrade;
        T.BusinessKey = FGuid::NewGuid();
        T.CommitBatch = ++Batch;
        T.Burn = MintInstead ? 0 : Cost;
        T.Mint = MintInstead;
        T.Gold.Emplace(LDEconomy::FGoldChange{Owner, Ledger.Account(Owner)->Revision, -Cost});
        const LDEconomy::FItem* Item = Ledger.Item(Instance);
        T.Upgrade.Emplace(LDEconomy::FUpgradeItem{Instance, Item ? Item->Revision : 0, Owner, Ledger.InventoryRevision(Owner)});
        return Ledger.Prepare(T);
    };
    const FGuid First = Buy(P1, 0, PureStats->BuyPrice);
    const FGuid Second = Buy(P1, 1, PureStats->BuyPrice);
    const FGuid Other = Buy(P2, 0, PureStats->BuyPrice);
    if (!Test.TestTrue(TEXT("Three items are bought"), First.IsValid() && Second.IsValid() && Other.IsValid())) { return false; }
    bGood &= Test.TestTrue(TEXT("An upgrade that mints instead of burning has the wrong shape"),
        Upgrade(P1, First, 240, 240).Error == LDEconomy::EPrepareError::InvalidTransaction);
    bGood &= Test.TestTrue(TEXT("Another contender's item cannot be upgraded"), Upgrade(P1, Other, 240).Error == LDEconomy::EPrepareError::NotOwner);
    const uint64 InventoryBefore = Ledger.InventoryRevision(P1);
    const int64 GoldBefore = Ledger.Account(P1)->Gold;
    LDEconomy::FPrepareResult Done = Upgrade(P1, First, 240);
    if (!Test.TestTrue(TEXT("The first upgrade prepares"), Done.IsSuccess())) { return false; }
    Ledger.ApplyPrepared(MoveTemp(Done.Value.GetValue()));
    bGood &= Test.TestTrue(TEXT("The upgrade burns its cost, marks the instance, advances item and inventory revisions and spends the account's upgrade"),
        Ledger.Account(P1)->Gold == GoldBefore - 240 && Ledger.Item(First)->bUpgraded && Ledger.Item(First)->Revision == 2
        && Ledger.InventoryRevision(P1) == InventoryBefore + 1 && Ledger.Account(P1)->bUpgradeUsed && !Ledger.Item(Second)->bUpgraded);
    bGood &= Test.TestTrue(TEXT("A second upgrade of the account is refused"), Upgrade(P1, Second, 240).Error == LDEconomy::EPrepareError::UpgradeUsed);
    // Selling the upgraded item loses the upgrade at the ordinary sell price; the spent upgrade never returns.
    LDEconomy::FTransaction Sell;
    Sell.Cause = LDEconomy::ECause::Sell;
    Sell.BusinessKey = FGuid::NewGuid();
    Sell.CommitBatch = ++Batch;
    Sell.Mint = PureStats->SellGold;
    Sell.Gold.Emplace(LDEconomy::FGoldChange{P1, Ledger.Account(P1)->Revision, PureStats->SellGold});
    Sell.Consume.Emplace(LDEconomy::FConsumeItem{First, 1, P1, Ledger.InventoryRevision(P1)});
    bGood &= Test.TestTrue(TEXT("Selling with the pre-upgrade item revision is stale"),
        Ledger.Prepare(Sell).Error == LDEconomy::EPrepareError::ItemStale);
    Sell.Consume->ExpectedItemRevision = 2;
    LDEconomy::FPrepareResult Sold = Ledger.Prepare(Sell);
    if (!Test.TestTrue(TEXT("The upgraded item sells at its ordinary price"), Sold.IsSuccess())) { return false; }
    Ledger.ApplyPrepared(MoveTemp(Sold.Value.GetValue()));
    bGood &= Test.TestTrue(TEXT("The upgrade left with the item and stays spent"), !Ledger.Item(First) && Ledger.Account(P1)->bUpgradeUsed
        && Upgrade(P1, Second, 240).Error == LDEconomy::EPrepareError::UpgradeUsed);
    bGood &= Test.TestTrue(TEXT("Another account still has its own upgrade"), Upgrade(P2, Other, 240).IsSuccess());
    bGood &= Test.TestTrue(FString::Printf(TEXT("The ledger reconciles (%s)"), *Error), Ledger.Reconcile(Error));
    FString Evidence;
    bGood &= Test.TestTrue(TEXT("Evidence exports the upgrade state"), LDEconomy::ExportEvidence(Ledger, 2000, Evidence, Error)
        && Evidence.Contains(TEXT("upgradeUsed")) && Evidence.Contains(TEXT("\"Upgrade\"")));

    // Wire: the new terms are part of request identity.
    FLDShopCommand A;
    A.Kind = ELDShopCommandKind::Upgrade;
    A.UpgradePrice = 240;
    FLDShopCommand B = A;
    B.UpgradePrice = 239;
    bGood &= Test.TestFalse(TEXT("Upgrade commands differing in price differ in terms"), A.SameTerms(B));
    FLDChoiceCommand C;
    FLDChoiceCommand D = C;
    D.bReroll = true;
    bGood &= Test.TestFalse(TEXT("A reroll differs in terms from a choice"), C.SameTerms(D));
    bGood &= Test.TestTrue(TEXT("Upgrade is a known shop kind"), LDGrowthWire::IsKnownShopKind(ELDShopCommandKind::Upgrade));
    return bGood;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthWireTest, "Lockdown.Rules.DemoGrowthWire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthWireTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunWire(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthLedgerTest, "Lockdown.Rules.DemoGrowthLedger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthLedgerTest::RunTest(const FString& Parameters)
{
    const bool bLedger = GrowthTestRunLedger(*this);
    const bool bEvidence = GrowthTestRunLedgerEvidence(*this);
    const bool bFixture = GrowthTestRunDevFixtureLedger(*this);
    return bLedger && bEvidence && bFixture;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthRequestsTest, "Lockdown.Rules.DemoGrowthRequests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthRequestsTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunRequests(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthProgressionTest, "Lockdown.Rules.DemoGrowthProgression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthProgressionTest::RunTest(const FString& Parameters)
{
    const bool bProgression = GrowthTestRunProgression(*this);
    const bool bFixture = GrowthTestRunDevFixture(*this);
    return bProgression && bFixture;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthOpeningTest, "Lockdown.Rules.DemoGrowthOpening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthOpeningTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunOpening(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthOpeningPoolTest, "Lockdown.Rules.DemoGrowthOpeningPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthOpeningPoolTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunOpeningPool(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthPrivacySchemaTest, "Lockdown.Rules.DemoGrowthPrivacySchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthPrivacySchemaTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunPrivacy(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAccountRetireAwaitsRewardTest, "Lockdown.Rules.DemoGrowthRetireAwaitsReward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDAccountRetireAwaitsRewardTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated retirement world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Economy = World->SpawnActor<ALDEconomyDirector>();
    if (!Mode || !Public || !Driver || !Economy) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Economy = Economy;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Killer = FGuid::NewGuid();
    const FGuid Stayer = FGuid::NewGuid();
    // The killer's fatal hit was locked while he was in the match; he left before its reward was retried.
    for (const FGuid& Id : {Killer, Stayer})
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = Id == Killer ? ELDLifePhase::Left : ELDLifePhase::Unspawned;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    Economy->Match = Mode->MatchContext;
    Economy->Roster = {Killer, Stayer};
    if (!TestTrue(TEXT("The match ledger opens"), Economy->Ledger.Initialize(Mode->MatchContext.LedgerMatch, Economy->Roster, 300, Error)))
    { AddError(Error); return false; }
    FLDCommittedDeath Owed;
    Owed.Key = {Mode->MatchContext.LedgerMatch, {ELDVictimKind::CampMember, FGuid::NewGuid(), 1}};
    Owed.CreditOwner = Killer;
    Owed.RewardBasis.bCreditOwnerEligibleAtDeath = true;
    Driver->RetryRewards.Add(Owed);
    World->Tick(LEVELTICK_TimeOnly, .025f);

    // The reward the retry would mint into the killer's account at the next boundary.
    const auto RetryMint = [&]()
    {
        const LDEconomy::FAccount* Account = Economy->Ledger.Account(Killer);
        LDEconomy::FTransaction Mint;
        Mint.Cause = LDEconomy::ECause::DeathReward;
        Mint.BusinessKey = FGuid::NewGuid();
        Mint.CommitBatch = Mode->AuthorityStep.StepSerial + 1;
        Mint.Mint = 20;
        Mint.Gold.Emplace(LDEconomy::FGoldChange{Killer, Account ? Account->Revision : 0, 20});
        return Economy->Ledger.Prepare(Mint);
    };
    const auto RunTransactions = [&](int64 EndAtMs)
    {
        const bool Ran = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, EndAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions,
                [&](const FLDGameplayWritePermit& Permit) { return Economy->RetireLeftAccounts(*Mode, Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!Ran) { AddError(FString::Printf(TEXT("Transactions boundary failed: %s"), *Error)); }
        return Ran;
    };

    if (!RunTransactions(10)) { return false; }
    const LDEconomy::FAccount* Account = Economy->Ledger.Account(Killer);
    TestTrue(TEXT("A contender who left keeps his account open while a reward owed to him awaits its retry"),
        Account && !Account->bRetired && !Economy->Retired.Contains(Killer));
    TestTrue(TEXT("The owed retry can still be paid into that account"), RetryMint().IsSuccess());
    TestTrue(TEXT("A present contender is never retired"),
        Economy->Ledger.Account(Stayer) && !Economy->Ledger.Account(Stayer)->bRetired);

    // The next boundary's DeathsAndRewards settled the retry; retirement then proceeds.
    Driver->RetryRewards.Reset();
    if (!RunTransactions(20)) { return false; }
    Account = Economy->Ledger.Account(Killer);
    TestTrue(TEXT("Once nothing is owed, the account retires through its ledger entry"),
        Account && Account->bRetired && Economy->Retired.Contains(Killer));
    TestTrue(TEXT("A retired account receives no later gold"),
        RetryMint().Error == LDEconomy::EPrepareError::RetiredAccount);
    return true;
}

// DEV test commands (#78): shape, the fixture gate, admission through the growth request stream, the Transactions commit
// of grants through the reward composite (ledger cause DevFixture), stale match/life and Left refusals, replay without a
// second commit, cooldown reset on the persistent tracks and the pure refill/reset parts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoDevTestCommandsTest, "Lockdown.Rules.DemoDevTestCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoDevTestCommandsTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const FLDDemoProgressionDefinition& Progression = Catalog->Get().Progression;
    if (!TestTrue(TEXT("The catalog has a level-2 threshold above zero"),
        Progression.XPThresholds.Num() >= 2 && Progression.XPThresholds[1] > 1))
    {
        return false;
    }
    const int64 SubLevelXP = Progression.XPThresholds[1] - 1;

    // ---- Shape ----
    const auto Command = [](uint32 Generation, uint32 Life, uint64 RequestId, ELDDevTestOp Op, int64 Amount)
    {
        FLDDevTestCommand Out;
        Out.Generation = Generation;
        Out.Life = Life;
        Out.RequestId = RequestId;
        Out.Op = Op;
        Out.Amount = Amount;
        return Out;
    };
    GrowthTestReason(*this, TEXT("A positive XP grant is a valid shape"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, 5)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("Request id 0 is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 0, ELDDevTestOp::GrantGold, 5)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("An unknown op is refused, not mapped to a neighbour"),
        LDDevTest::ValidateShape(Command(1, 1, 1, static_cast<ELDDevTestOp>(99), 0)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A zero grant is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantGold, 0)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A negative grant is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, -3)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A refill with an amount is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::RefillResources, 1)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A cooldown reset without an amount is a valid shape"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::ResetCooldowns, 0)), ELDGrowthReason::None);
    TestFalse(TEXT("Commands that differ only in amount have different terms"),
        Command(1, 1, 1, ELDDevTestOp::GrantGold, 5).SameTerms(Command(1, 1, 1, ELDDevTestOp::GrantGold, 6)));
    const auto Attribute = [&Command](uint64 RequestId, ELDDemoBonusStat Stat, int64 Amount, uint32 Generation = 1)
    {
        FLDDevTestCommand Out = Command(Generation, 1, RequestId, ELDDevTestOp::SetAttribute, Amount);
        Out.Stat = uint8(Stat);
        return Out;
    };
    GrowthTestReason(*this, TEXT("A DEV attribute term of 0 (removal) is a valid shape"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::CritChanceBonusRatio, 0)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("A negative DEV attribute term is refused"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::ADFlat, -1)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("The timed-only ADBonusRatio is no DEV attribute stat"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::ADBonusRatio, 100)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A stat outside the enum is refused"),
        LDDevTest::ValidateShape(Attribute(1, static_cast<ELDDemoBonusStat>(40), 100)), ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand ClearNamed = Command(1, 1, 1, ELDDevTestOp::ClearAttributes, 0);
    ClearNamed.Stat = uint8(ELDDemoBonusStat::APFlat);
    GrowthTestReason(*this, TEXT("Only SetAttribute names a stat"), LDDevTest::ValidateShape(ClearNamed), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A clear with an amount is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::ClearAttributes, 5)), ELDGrowthReason::NumericDomain);
    TestFalse(TEXT("Commands that differ only in stat have different terms"),
        Attribute(1, ELDDemoBonusStat::ADFlat, 5).SameTerms(Attribute(1, ELDDemoBonusStat::APFlat, 5)));

    // ---- Pure combat parts ----
    FLDBodyCombatState::FResources Pool;
    Pool.HpP2 = 40;
    Pool.MaxHpP2 = 100;
    Pool.MpP2 = 0;
    Pool.MaxMpP2 = 60;
    Pool.ShieldP2 = 7;
    Pool.Revision = 3;
    TestTrue(TEXT("A refill reports a change"), ALDCombatDirector::DevRefillPool(Pool));
    TestTrue(TEXT("A refill sets HP and MP to their maxima, keeps the shield and advances the resource revision once"),
        Pool.HpP2 == 100 && Pool.MpP2 == 60 && Pool.ShieldP2 == 7 && Pool.Revision == 4);
    TestFalse(TEXT("A full pool refills without a change or a revision"), ALDCombatDirector::DevRefillPool(Pool) || Pool.Revision != 4);
    TArray<FLDCombatTrack> Tracks;
    FLDCombatTrack& Cooling = Tracks.AddDefaulted_GetRef();
    Cooling.IssuedCooldownId = FGuid(0xD7, 0, 0, 1);
    Cooling.Kind = ELDCombatSourceKind::Learned;
    Cooling.ReadyAtMs = 9000;
    Cooling.LastDurationMs = 8000;
    FLDCombatTrack& Ready = Tracks.AddDefaulted_GetRef();
    Ready.IssuedCooldownId = FGuid(0xD7, 0, 0, 2);
    Ready.ReadyAtMs = 500;
    TestEqual(TEXT("Only the cooling track is reset"), ALDCombatDirector::DevResetTracks(Tracks, 1000), 1);
    TestTrue(TEXT("The reset track is ready now and keeps identity, kind and last duration"),
        Tracks[0].ReadyAtMs == 1000 && Tracks[0].IssuedCooldownId == FGuid(0xD7, 0, 0, 1)
        && Tracks[0].Kind == ELDCombatSourceKind::Learned && Tracks[0].LastDurationMs == 8000);
    TestEqual(TEXT("A track already ready keeps its earlier end"), Tracks[1].ReadyAtMs, int64(500));
    TArray<FLDDemoStatBonus> Layer;
    TestTrue(TEXT("A first DEV term is a change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::MoveSpeedBonusRatio, 500));
    TestTrue(TEXT("A second DEV term is a change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 2000));
    TestTrue(TEXT("Range travels as whole cm"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::AttackRangeCmFlat, 50));
    TestTrue(TEXT("DEV terms stay sorted by stat, range as a Cm rational with no Amount"),
        Layer.Num() == 3 && Layer[0].Stat == ELDDemoBonusStat::ADFlat && Layer[0].Amount == 2000
        && Layer[1].Stat == ELDDemoBonusStat::AttackRangeCmFlat && Layer[1].Amount == 0 && Layer[1].Cm.Numerator == 50
        && Layer[1].Cm.Denominator == 1 && Layer[2].Stat == ELDDemoBonusStat::MoveSpeedBonusRatio);
    TestFalse(TEXT("The same term again is no change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 2000));
    TestTrue(TEXT("A new amount replaces the stat's term"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 300)
        && Layer.Num() == 3 && Layer[0].Amount == 300);
    TestTrue(TEXT("Amount 0 removes only that stat's term"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 0)
        && Layer.Num() == 2 && Layer[0].Stat == ELDDemoBonusStat::AttackRangeCmFlat);
    TestFalse(TEXT("Removing an absent term is no change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::APFlat, 0));
    TestTrue(TEXT("A clear empties the layer"), ALDCombatDirector::DevApplyAttribute(Layer, true, ELDDemoBonusStat::ADFlat, 0) && Layer.IsEmpty());
    TestFalse(TEXT("Clearing an empty layer is no change"), ALDCombatDirector::DevApplyAttribute(Layer, true, ELDDemoBonusStat::ADFlat, 0));

    // ---- Pure projection: the DEV layer is its own additive row ----
    const auto DevTerm = [](ELDDemoBonusStat Stat, int64 Amount)
    {
        FLDDemoStatBonus Out;
        Out.Stat = Stat;
        if (Stat == ELDDemoBonusStat::AttackRangeCmFlat) { Out.Cm.Numerator = Amount; Out.Cm.Denominator = 1; }
        else { Out.Amount = Amount; }
        return Out;
    };
    const auto ProjectWith = [&Catalog](TArray<FLDDemoStatBonus> Dev, TArray<FLDAttributeCardValue> Cards = {})
    {
        FLDAttributeBuildValues Values(1, {}, MoveTemp(Cards), {});
        Values.DevBonuses = MoveTemp(Dev);
        return LDAttributeProjection::Project(Values, *Catalog);
    };
    const FLDAttributeProjectionResult Plain = ProjectWith({});
    const FLDAttributeProjectionResult WithDev = ProjectWith({DevTerm(ELDDemoBonusStat::ADFlat, 2000),
        DevTerm(ELDDemoBonusStat::AttackSpeedBonusRatio, 2500)});
    if (!TestTrue(TEXT("Level-1 projections with and without a DEV layer succeed"), Plain.IsSuccess() && WithDev.IsSuccess()))
    {
        return false;
    }
    const FLDAttributeProjection& PlainValue = Plain.Value.GetValue();
    const FLDAttributeProjection& DevValue = WithDev.Value.GetValue();
    const FLDAttributeContributions& DevAD = DevValue.Contributions[int32(ELDProjectedAttribute::AD)];
    TestTrue(TEXT("DEV AD +20 points adds exactly 2000 P2 and shows as the Dev row, not as a build source"),
        DevValue.Final.ADP2 == PlainValue.Final.ADP2 + 2000 && DevAD.Dev.Flat.Numerator == 20 && DevAD.Dev.Flat.Denominator == 1
        && DevAD.Equipment.Flat.Numerator == 0 && DevAD.PermanentChoices.Flat.Numerator == 0);
    const FLDAttributeContributions& DevSpeed = DevValue.Contributions[int32(ELDProjectedAttribute::AttackSpeed)];
    TestTrue(TEXT("A DEV ratio is an additive ratio of the Dev row"),
        DevSpeed.Dev.AdditiveRatio.Numerator * 4 == DevSpeed.Dev.AdditiveRatio.Denominator
        && DevValue.Final.MaxHPP2 == PlainValue.Final.MaxHPP2);
    FLDTemporaryAttributeTerms Timed;
    FLDDemoStatBonus TimedAD;
    TimedAD.Stat = ELDDemoBonusStat::ADFlat;
    TimedAD.Amount = 100;
    Timed.Bonuses.Add(TimedAD);
    const FLDAttributeProjectionResult TimedDev = LDAttributeProjection::ApplyTemporary(DevValue, Timed, *Catalog);
    TestTrue(TEXT("Temporary terms stack on top of the DEV row"),
        TimedDev.IsSuccess() && TimedDev.Value->Final.ADP2 == DevValue.Final.ADP2 + 100);
    const auto ProjectionError = [this](const TCHAR* What, const FLDAttributeProjectionResult& Result, ELDAttributeProjectionError Expected)
    {
        TestEqual(What, int32(Result.Error.Code), int32(Expected));
    };
    ProjectionError(TEXT("A stat named twice in the DEV layer"),
        ProjectWith({DevTerm(ELDDemoBonusStat::ADFlat, 1), DevTerm(ELDDemoBonusStat::ADFlat, 2)}), ELDAttributeProjectionError::DuplicateSource);
    ProjectionError(TEXT("ADBonusRatio in the DEV layer"), ProjectWith({DevTerm(ELDDemoBonusStat::ADBonusRatio, 100)}),
        ELDAttributeProjectionError::InputDomain);
    ProjectionError(TEXT("A negative DEV term"), ProjectWith({DevTerm(ELDDemoBonusStat::APFlat, -100)}),
        ELDAttributeProjectionError::InputDomain);
    const FLDDemoContentParameters& Bounds = Catalog->Get().Parameters;
    ProjectionError(TEXT("A DEV term past the catalog maximum refuses rather than clamps"),
        ProjectWith({DevTerm(ELDDemoBonusStat::MaxHPFlat, Bounds.MaxHP.MaxP2)}), ELDAttributeProjectionError::OutOfBounds);

    // ---- Authority: isolated match with the growth domain ----
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated DEV test world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Economy = World->SpawnActor<ALDEconomyDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    auto* Vendor = World->SpawnActor<ALDVendorDirector>();
    if (!Mode || !Public || !Driver || !Economy || !Development || !Vendor) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    // The directors resolve the core through the world's authority game mode, exactly as in a hosted match.
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {4, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Economy = Economy;
    Mode->Development = Development;
    Mode->Vendor = Vendor;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Tester(0xD7, 0, 0, 0x10);
    const FGuid Leaver(0xD7, 0, 0, 0x20);
    for (const FGuid& Id : {Tester, Leaver})
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = Id == Leaver ? ELDLifePhase::Left : ELDLifePhase::Unspawned;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    const int64 StartingGold = Catalog->Get().Parameters.StartingGold;
    Economy->Match = Mode->MatchContext;
    Economy->Roster = {Tester, Leaver};
    Economy->Content = Catalog;
    if (!TestTrue(TEXT("The match ledger opens"), Economy->Ledger.Initialize(Mode->MatchContext.LedgerMatch, Economy->Roster,
        StartingGold, Error))) { AddError(Error); return false; }
    if (!TestTrue(TEXT("Growth opens for the roster"), Development->BeginMatch(Mode->MatchContext, Economy->Roster, Catalog, Error)))
    { AddError(Error); return false; }
    // The tester's persistent combat record: one learned track still cooling down, another one already ready.
    FLDCombatEntityRecord& CombatRecord = Driver->Records.Add(Tester);
    CombatRecord.Kind = ELDVictimKind::Contender;
    CombatRecord.Entity = Tester;
    CombatRecord.Build.Level = 1;
    CombatRecord.Projection = PlainValue;
    FLDCombatTrack& Learned = CombatRecord.Tracks.AddDefaulted_GetRef();
    Learned.IssuedCooldownId = FGuid(0xD7, 0, 1, 1);
    Learned.Kind = ELDCombatSourceKind::Learned;
    Learned.ReadyAtMs = 60000;
    FLDCombatTrack& Spent = CombatRecord.Tracks.AddDefaulted_GetRef();
    Spent.IssuedCooldownId = FGuid(0xD7, 0, 1, 2);
    Spent.Kind = ELDCombatSourceKind::Learned;
    Spent.ReadyAtMs = 0;
    World->Tick(LEVELTICK_TimeOnly, .025f);

    // One 25 ms tick of native time; every boundary below advances 1 ms inside it.
    int64 StepAtMs = 0;
    const auto RunTransactions = [&]()
    {
        StepAtMs += 1;
        Error.Reset();
        const ELDCommitReject Begin = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs);
        if (Begin != ELDCommitReject::None)
        {
            AddError(FString::Printf(TEXT("The boundary at %lld ms did not begin: reject %d"), StepAtMs, int32(Begin)));
            return false;
        }
        const bool Ran = Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions,
                [&](const FLDGameplayWritePermit& Permit) { return Economy->ProcessTransactions(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!Ran) { AddError(FString::Printf(TEXT("Transactions boundary failed: %s"), *Error)); }
        return Ran;
    };
    const auto Gold = [&](const FGuid& Id) { const LDEconomy::FAccount* A = Economy->Ledger.Account(Id); return A ? A->Gold : -1; };
    const auto XP = [&]() { const FLDProgressionState* P = Development->FindProgression(Tester); return P ? P->CumulativeXP : -1; };
    const auto Status = [&](const FString& What, const FLDDevTestResult& Result, ELDAdmissionStatus Expected, ELDGrowthReason Reason)
    {
        GrowthTestEnum(*this, What + TEXT(" (status)"), Result.Status, Expected);
        GrowthTestReason(*this, What + TEXT(" (reason)"), LDGrowthWire::Reason(Result.Reason), Reason);
    };
    const auto LastReceipt = [&](const FGuid& Id) -> const FLDDevTestReceipt*
    {
        const auto* Record = Economy->DevTests.Find(Id);
        return Record && !Record->Unpublished.IsEmpty() ? &Record->Unpublished.Last() : nullptr;
    };
    const uint32 Gen = Mode->MatchContext.Generation;

    // The gate: the rules run carries no -LDDevGrowthFixture switch, so the public entry admits nothing and says why.
    TestFalse(TEXT("The rules run carries no -LDDevGrowthFixture switch"), LDDevGrowthFixture::IsSwitchPresent());
    const FLDDevTestResult Gated = Economy->QueueDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 50), true);
    Status(TEXT("Without the switch a DEV test command is refused"), Gated, ELDAdmissionStatus::AdmissionRejected,
        ELDGrowthReason::NotAuthority);
    TestTrue(TEXT("The refusal names the switch and no stream was opened"),
        Gated.Detail.Contains(TEXT("-LDDevGrowthFixture")) && !Economy->DevTests.Contains(Tester));

    // Admission behind the gate.
    Status(TEXT("A command of another match"), Economy->AdmitDevTest(Tester, Command(Gen + 1, 1, 1, ELDDevTestOp::GrantGold, 50)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OldMatch);
    Status(TEXT("A malformed command"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NumericDomain);
    Status(TEXT("A caller that is not a contender"),
        Economy->AdmitDevTest(FGuid(0xD7, 0, 0, 0x99), Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 50)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotContender);
    Status(TEXT("A first request that skips id 1"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantGold, 50)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OutOfOrder);
    const FLDDevTestCommand GoldGrant = Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 50);
    const FLDDevTestResult Accepted = Economy->AdmitDevTest(Tester, GoldGrant);
    Status(TEXT("Request 1 is accepted"), Accepted, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    TestTrue(TEXT("The accepted request reserves its sequence"), Accepted.bSequenceReserved);
    Status(TEXT("The same request again"), Economy->AdmitDevTest(Tester, GoldGrant), ELDAdmissionStatus::AcceptedPending,
        ELDGrowthReason::None);
    Status(TEXT("Request 1 with other terms"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 51)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict);
    Status(TEXT("Request 2 while 1 is pending"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantGold, 5)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestInFlight);
    TestEqual(TEXT("Nothing is minted before the Transactions phase"), Gold(Tester), StartingGold);

    if (!RunTransactions()) { return false; }
    const FLDDevTestReceipt* Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The gold grant commits once with an applied receipt"), Receipt && Receipt->Header.RequestId == 1
        && Receipt->Header.Outcome == ELDReceiptOutcome::Applied && Receipt->Op == ELDDevTestOp::GrantGold);
    TestEqual(TEXT("The account holds the minted gold"), Gold(Tester), StartingGold + 50);
    const TArray<LDEconomy::FDevFixtureMint>& Mints = Economy->Ledger.DevFixtureGrants();
    TestTrue(TEXT("The mint is a DevFixture ledger entry of the tester"),
        Mints.Num() == 1 && Mints[0].Account == Tester && Mints[0].Gold == 50);
    TestTrue(FString::Printf(TEXT("The ledger reconciles after the grant (%s)"), *Error), Economy->Ledger.Reconcile(Error));
    const FLDDevTestResult Replay = Economy->AdmitDevTest(Tester, GoldGrant);
    Status(TEXT("A replay of the committed request"), Replay, ELDAdmissionStatus::Final, ELDGrowthReason::None);
    TestTrue(TEXT("The replay answers with the original receipt"), Replay.bHasReceipt && Replay.Receipt.Header.RequestId == 1
        && Replay.Receipt.Header.Outcome == ELDReceiptOutcome::Applied);
    if (!RunTransactions()) { return false; }
    TestEqual(TEXT("The replay commits nothing a second time"), Gold(Tester), StartingGold + 50);

    // XP below the next level: cumulative XP only; the level and the build stay.
    Status(TEXT("Request 2: XP below the next level"),
        Economy->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantExperience, SubLevelXP)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The XP grant applies"), Receipt && Receipt->Header.RequestId == 2 && Receipt->Header.Outcome == ELDReceiptOutcome::Applied);
    TestEqual(TEXT("Cumulative XP holds the grant"), XP(), SubLevelXP);
    TestTrue(TEXT("The level is unchanged below the threshold"),
        Development->FindBuild(Tester) && Development->FindBuild(Tester)->Level == 1);
    TestEqual(TEXT("An XP grant mints no gold"), Gold(Tester), StartingGold + 50);

    // A command of an earlier life never applies.
    Status(TEXT("Request 3 names life 0"), Economy->AdmitDevTest(Tester, Command(Gen, 0, 3, ELDDevTestOp::GrantGold, 9)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The old-life command is rejected"), Receipt && Receipt->Header.RequestId == 3
        && Receipt->Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::OldLife);
    TestEqual(TEXT("The rejected command minted nothing"), Gold(Tester), StartingGold + 50);

    // Refill needs a living body: a contender that is not Alive is refused (the Alive path is the pure DevRefillPool above).
    Status(TEXT("Request 4: refill while unspawned"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 4, ELDDevTestOp::RefillResources, 0)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("A refill without a living phase is rejected as Dead"), Receipt && Receipt->Header.RequestId == 4
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::Dead);
    // Cooldown reset on the persistent record, at the commit boundary.
    Status(TEXT("Request 5: reset cooldowns"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 5, ELDDevTestOp::ResetCooldowns, 0)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The reset applies"), Receipt && Receipt->Header.RequestId == 5 && Receipt->Header.Outcome == ELDReceiptOutcome::Applied);
    const FLDCombatEntityRecord& AfterReset = Driver->Records.FindChecked(Tester);
    TestTrue(TEXT("The cooling learned track is ready at the commit boundary"),
        AfterReset.Tracks.Num() == 2 && AfterReset.Tracks[0].ReadyAtMs == StepAtMs
        && AfterReset.Tracks[0].IssuedCooldownId == FGuid(0xD7, 0, 1, 1));
    TestEqual(TEXT("A ready track keeps its earlier end"), AfterReset.Tracks[1].ReadyAtMs, int64(0));

    // A contender that left gets nothing.
    Status(TEXT("The leaver's request 1"), Economy->AdmitDevTest(Leaver, Command(Gen, 1, 1, ELDDevTestOp::GrantGold, 50)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    const int64 LeaverGold = Gold(Leaver);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Leaver);
    TestTrue(TEXT("A command of a contender that left is rejected as LeftMatch"), Receipt
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::LeftMatch);
    TestEqual(TEXT("The leaver's account received nothing"), Gold(Leaver), LeaverGold);

    // ---- Learn: any legal card as the head opportunity's choice, with every choice rule ----
    const auto Learn = [&](uint64 RequestId, const TCHAR* Id, uint8 Slot)
    {
        FLDDevTestCommand Out = Command(Gen, 1, RequestId, ELDDevTestOp::LearnOption, 0);
        FLDDefinitionWire::Encode(FName(Id), Out.Definition);
        Out.Slot = Slot;
        return Out;
    };
    FLDDevTestCommand BadSlot = Learn(1, TEXT("attr_protein"), 3);
    GrowthTestReason(*this, TEXT("A learn names slot 0..2"), LDDevTest::ValidateShape(BadSlot), ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand NoDefinition = Command(Gen, 1, 1, ELDDevTestOp::LearnOption, 0);
    GrowthTestReason(*this, TEXT("A learn names a definition"), LDDevTest::ValidateShape(NoDefinition), ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand SellNamed = Command(Gen, 1, 1, ELDDevTestOp::SellItem, 0);
    SellNamed.Slot = 1;
    FLDDefinitionWire::Encode(FName(TEXT("item_edge_blade")), SellNamed.Definition);
    GrowthTestReason(*this, TEXT("A sell names a slot, not a definition"), LDDevTest::ValidateShape(SellNamed),
        ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand SellNoSlot = Command(Gen, 1, 1, ELDDevTestOp::SellItem, 0);
    GrowthTestReason(*this, TEXT("A sell names slot 1 or 2"), LDDevTest::ValidateShape(SellNoSlot), ELDGrowthReason::InvalidRequest);

    const auto Commit = [&](const TCHAR* What, const FLDDevTestCommand& Sent, ELDGrowthReason Expected) -> const FLDDevTestReceipt*
    {
        Status(What, Economy->AdmitDevTest(Tester, Sent), ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
        if (!RunTransactions()) { return nullptr; }
        const FLDDevTestReceipt* Final = LastReceipt(Tester);
        if (TestTrue(FString(What) + TEXT(" finalizes"), Final && Final->Header.RequestId == Sent.RequestId))
        {
            GrowthTestReason(*this, FString(What) + TEXT(" (commit)"), LDGrowthWire::Reason(Final->Header.Reason), Expected);
            return Final;
        }
        return nullptr;
    };
    if (!Commit(TEXT("Request 6: learn while unspawned"), Learn(6, TEXT("attr_protein"), 0), ELDGrowthReason::LifePhaseChanged))
    { return false; }
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Dead;
    const FLDProgressionState* Before = Development->FindProgression(Tester);
    const uint8 ConsumedBefore = Before ? Before->ConsumedThrough : 0;
    if (!Commit(TEXT("Request 7: an id that is no legal option"), Learn(7, TEXT("attr_unknown"), 0), ELDGrowthReason::InvalidOption))
    { return false; }
    if (!Commit(TEXT("Request 8: replacing an empty Q"), Learn(8, TEXT("attr_protein"), 1), ELDGrowthReason::ReplacementMismatch))
    { return false; }

    // ---- DEV attribute layer: Combat's installed build, without a growth or inventory revision ----
    const FLDCombatEntityRecord& Fighter = Driver->Records.FindChecked(Tester);
    const FLDExpectedBuildVersions InstalledBefore = Fighter.Installed;
    const uint64 GrowthRevisionBefore = Development->FindBuild(Tester)->Revision;
    const FLDDevTestReceipt* AttrReceipt = Commit(TEXT("Request 9: DEV AD +20 while dead"),
        Attribute(9, ELDDemoBonusStat::ADFlat, 2000, Gen), ELDGrowthReason::None);
    if (!AttrReceipt) { return false; }
    TestTrue(TEXT("The receipt names the stat and amount"), AttrReceipt->Stat == uint8(ELDDemoBonusStat::ADFlat) && AttrReceipt->Amount == 2000);
    TestTrue(TEXT("The persistent build holds the DEV term and its projection shows it as the Dev row"),
        Fighter.Build.DevBonuses.Num() == 1 && Fighter.Build.DevBonuses[0].Amount == 2000
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2 + 2000
        && Fighter.Projection.Contributions[int32(ELDProjectedAttribute::AD)].Dev.Flat.Numerator == 20);
    TestTrue(TEXT("Neither Combat's installed revisions nor the growth build revision move"),
        Fighter.Installed.BuildRevision == InstalledBefore.BuildRevision
        && Fighter.Installed.InventoryRevision == InstalledBefore.InventoryRevision
        && Development->FindBuild(Tester)->Revision == GrowthRevisionBefore);
    const FLDDevTestReceipt* TooMuch = Commit(TEXT("Request 10: DEV max HP past the catalog maximum"),
        Attribute(10, ELDDemoBonusStat::MaxHPFlat, Bounds.MaxHP.MaxP2, Gen), ELDGrowthReason::NumericDomain);
    TestTrue(TEXT("The refusal explains the bound and the layer is unchanged"), TooMuch && TooMuch->Detail.Contains(TEXT("MaxHP"))
        && Fighter.Build.DevBonuses.Num() == 1 && Fighter.Projection.Final.MaxHPP2 == PlainValue.Final.MaxHPP2);
    FLDCombatCaller GrowthCaller;
    GrowthCaller.Match = Mode->GetMatchContext();
    GrowthCaller.Kind = ELDVictimKind::Contender;
    GrowthCaller.EntityId = Tester;
    FLDBuildProjectionInput Smuggled = Fighter.Build;
    Smuggled.ResultBuildRevision = Fighter.Installed.BuildRevision + 1;
    TestEqual(TEXT("A Growth build change that names a DEV layer"),
        int32(Driver->PrepareBuildChange(GrowthCaller, Smuggled, Fighter.Installed).Error), int32(ELDCombatPrepareError::InvalidInput));

    const FLDDevTestReceipt* LearnedReceipt = Commit(TEXT("Request 11: an attribute card while dead"), Learn(11, TEXT("attr_protein"), 0),
        ELDGrowthReason::None);
    if (!LearnedReceipt) { return false; }
    TestTrue(TEXT("The receipt names the learned definition"), LearnedReceipt->Definition == FName(TEXT("attr_protein")));
    const FLDGrowthBuild* AfterLearn = Development->FindBuild(Tester);
    const FLDProgressionState* ProgressionAfter = Development->FindProgression(Tester);
    TestTrue(TEXT("The card is in the build and the opening opportunity is consumed"),
        AfterLearn && AfterLearn->Cards.Num() == 1 && AfterLearn->Cards[0].Id == FName(TEXT("attr_protein"))
        && AfterLearn->Cards[0].Count == 1 && ProgressionAfter && ProgressionAfter->ConsumedThrough == ConsumedBefore + 1
        && ProgressionAfter->Records.Num() == 1);
    TestTrue(TEXT("Combat installed the new build"), Driver->Records.FindChecked(Tester).Installed.BuildRevision == AfterLearn->Revision);
    const FLDAttributeProjectionResult CardOnly = ProjectWith({}, {FLDAttributeCardValue(TEXT("attr_protein"), 1)});
    TestTrue(TEXT("The real build change keeps the installed DEV term on top of the new card"),
        CardOnly.IsSuccess() && Fighter.Build.DevBonuses.Num() == 1 && Fighter.Build.AttributeCards.Num() == 1
        && Fighter.Projection.Final.ADP2 == CardOnly.Value->Final.ADP2 + 2000);
    const FLDDevTestReceipt* NoOpportunity = Commit(TEXT("Request 12: learn with no opportunity left"),
        Learn(12, TEXT("attr_all_nighter"), 0), ELDGrowthReason::NoOffer);
    TestTrue(TEXT("The refusal says to raise the level first"), NoOpportunity && NoOpportunity->Detail.Contains(TEXT("raise the level")));
    TestTrue(TEXT("Nothing more was learned"), Development->FindBuild(Tester)->Cards.Num() == 1);
    if (!Commit(TEXT("Request 13: clear the DEV layer"), Command(Gen, 1, 13, ELDDevTestOp::ClearAttributes, 0), ELDGrowthReason::None))
    { return false; }
    TestTrue(TEXT("A clear removes only the DEV term; the learned card stays"),
        Fighter.Build.DevBonuses.IsEmpty() && Fighter.Build.AttributeCards.Num() == 1
        && Fighter.Projection.Final.ADP2 == CardOnly.Value->Final.ADP2);

    // Trades need a living body; a dead contender is refused before any ledger change.
    FLDDevTestCommand Buy = Command(Gen, 1, 14, ELDDevTestOp::BuyItem, 0);
    FLDDefinitionWire::Encode(FName(TEXT("item_edge_blade")), Buy.Definition);
    const int64 GoldBeforeBuy = Gold(Tester);
    if (!Commit(TEXT("Request 14: buy while dead"), Buy, ELDGrowthReason::Dead)) { return false; }
    TestEqual(TEXT("A refused buy spends nothing"), Gold(Tester), GoldBeforeBuy);
    TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
    Economy->Ledger.OwnerItems(Tester, Owned);
    TestEqual(TEXT("A refused buy creates no item"), Owned.Num(), 0);

    // ---- Reset to the match start: DevReset ledger entries, the BeginMatch growth state and an empty Combat build ----
    GrowthTestReason(*this, TEXT("A reset names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::ResetToMatchStart, 5)), ELDGrowthReason::NumericDomain);
    // The tester holds one item bought through the ordinary ledger shape.
    {
        LDEconomy::FTransaction Bought;
        Bought.Cause = LDEconomy::ECause::Buy;
        Bought.BusinessKey = FGuid(0xD7, 0, 2, 1);
        Bought.CommitBatch = 1;
        LDEconomy::FGoldChange Paid;
        Paid.Account = Tester;
        Paid.ExpectedRevision = Economy->Ledger.Account(Tester)->Revision;
        Paid.Delta = -20;
        Bought.Gold.Emplace(Paid);
        Bought.Burn = 20;
        LDEconomy::FCreateItem Created;
        Created.Owner = Tester;
        Created.ExpectedInventoryRevision = Economy->Ledger.InventoryRevision(Tester);
        Created.Definition = FName(TEXT("item_edge_blade"));
        Created.Slot = 1;
        Bought.Create.Emplace(Created);
        LDEconomy::FPrepareResult Purchase = Economy->Ledger.Prepare(Bought);
        if (!TestTrue(FString::Printf(TEXT("The fixture purchase prepares (%s)"), *Purchase.Message), Purchase.IsSuccess())) { return false; }
        Economy->Ledger.ApplyPrepared(MoveTemp(Purchase.Value.GetValue()));
    }
    const int64 GoldBeforeReset = Gold(Tester);
    const uint64 BuildRevisionBeforeReset = Development->FindBuild(Tester)->Revision;
    const uint64 OfferRevisionBeforeReset = Development->FindProgression(Tester)->LastOfferRevision;
    const FLDDevTestReceipt* ResetReceipt = Commit(TEXT("Request 15: reset to the match start while dead"),
        Command(Gen, 1, 15, ELDDevTestOp::ResetToMatchStart, 0), ELDGrowthReason::None);
    if (!ResetReceipt) { return false; }
    TestTrue(TEXT("The reset receipt explains what changed"), ResetReceipt->Detail.Contains(TEXT("1 items removed")));
    Owned.Reset();
    Economy->Ledger.OwnerItems(Tester, Owned);
    TestTrue(TEXT("The reset holds the starting gold and no item"), Gold(Tester) == StartingGold && Owned.IsEmpty());
    const TArray<LDEconomy::FDevResetEntry>& ResetEntries = Economy->Ledger.DevResetEntries();
    TestTrue(TEXT("Two DevReset entries: the item consumed without a refund, then the gold change back to the start"),
        ResetEntries.Num() == 2 && ResetEntries[0].Account == Tester && ResetEntries[0].Gold == 0 && ResetEntries[0].Instance.IsValid()
        && ResetEntries[1].Instance == FGuid() && ResetEntries[1].Gold == StartingGold - GoldBeforeReset
        && ResetEntries[1].Serial == ResetEntries[0].Serial + 1);
    TestTrue(FString::Printf(TEXT("The ledger reconciles after the reset (%s)"), *Error), Economy->Ledger.Reconcile(Error));
    FString ResetEvidence;
    // The boundary may already have folded the entries into the audit prefix; the devFixture listing keeps them all.
    TSharedPtr<FJsonObject> ResetRoot;
    const TSharedPtr<FJsonObject>* ResetFixture = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* ResetRows = nullptr;
    TestTrue(FString::Printf(TEXT("The evidence lists both reset entries (%s)"), *Error),
        LDEconomy::ExportEvidence(Economy->Ledger, StartingGold, ResetEvidence, Error)
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResetEvidence), ResetRoot) && ResetRoot.IsValid()
        && ResetRoot->TryGetObjectField(TEXT("devFixture"), ResetFixture)
        && (*ResetFixture)->TryGetArrayField(TEXT("resets"), ResetRows) && ResetRows->Num() == 2);
    const FLDGrowthBuild* ResetBuild = Development->FindBuild(Tester);
    const FLDProgressionState* ResetProgression = Development->FindProgression(Tester);
    TestTrue(TEXT("Growth is back at level 1 with no XP, skills or cards and a higher build revision"),
        ResetBuild && ResetBuild->Level == 1 && ResetBuild->Skills.IsEmpty() && ResetBuild->Cards.IsEmpty()
        && ResetBuild->Revision > BuildRevisionBeforeReset && ResetProgression && ResetProgression->CumulativeXP == 0);
    TestTrue(TEXT("Only the opening opportunity is open, with a fresh opening offer"),
        ResetProgression && ResetProgression->EarnedThrough == 1 && ResetProgression->ConsumedThrough == 0
        && ResetProgression->Records.IsEmpty() && ResetProgression->Offer.IsSet() && ResetProgression->Offer->Ordinal == 1
        && ResetProgression->Offer->Revision > OfferRevisionBeforeReset && ResetProgression->Offer->Options.Num() == 3);
    TestTrue(TEXT("Combat installed the empty level-1 build at the growth and ledger revisions, without a DEV layer"),
        Fighter.Build.Level == 1 && Fighter.Build.AttributeCards.IsEmpty() && Fighter.Build.Actives.IsEmpty()
        && Fighter.Build.Equipment.IsEmpty() && Fighter.Build.DevBonuses.IsEmpty()
        && Fighter.Installed.BuildRevision == ResetBuild->Revision
        && Fighter.Installed.InventoryRevision == Economy->Ledger.InventoryRevision(Tester)
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2);
    TestTrue(TEXT("Every learned cooldown track is dropped"), !Fighter.Tracks.ContainsByPredicate(
        [](const FLDCombatTrack& Track) { return Track.Kind != ELDCombatSourceKind::Innate; }));
    TArray<FLDCombatTrack> Mixed;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    FLDCombatTrack& Innate = Mixed.AddDefaulted_GetRef();
    Innate.Kind = ELDCombatSourceKind::Innate;
    Innate.ReadyAtMs = 9000;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    TestTrue(TEXT("The pure track drop keeps only the innate dash, ready now"), ALDCombatDirector::DevDropTracks(Mixed, 1000) == 2
        && Mixed.Num() == 1 && Mixed[0].Kind == ELDCombatSourceKind::Innate && Mixed[0].ReadyAtMs == 1000);
    const FLDDevTestReceipt* Relearned = Commit(TEXT("Request 16: the fresh opening opportunity takes a card again"),
        Learn(16, TEXT("attr_protein"), 0), ELDGrowthReason::None);
    TestTrue(TEXT("After the reset the card is learned once more"), Relearned && Development->FindBuild(Tester)->Cards.Num() == 1
        && Development->FindProgression(Tester)->ConsumedThrough == 1);

    // ---- Match-wide ops: Listen Host connection only ----
    TestTrue(TEXT("Camp refresh and early capture are the match-wide ops"), LDDevTest::IsMatchWide(ELDDevTestOp::RefreshCamps)
        && LDDevTest::IsMatchWide(ELDDevTestOp::OpenCapture) && !LDDevTest::IsMatchWide(ELDDevTestOp::GrantGold));
    GrowthTestReason(*this, TEXT("A camp refresh names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::RefreshCamps, 1)), ELDGrowthReason::NumericDomain);
    Status(TEXT("A camp refresh from an owning client"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 17, ELDDevTestOp::RefreshCamps, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotAuthority);
    Status(TEXT("An early capture from an owning client"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 17, ELDDevTestOp::OpenCapture, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotAuthority);
    const auto HostCommit = [&](const TCHAR* What, const FLDDevTestCommand& Sent, ELDGrowthReason Expected) -> const FLDDevTestReceipt*
    {
        Status(What, Economy->AdmitDevTest(Tester, Sent, true), ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
        if (!RunTransactions()) { return nullptr; }
        const FLDDevTestReceipt* Final = LastReceipt(Tester);
        if (TestTrue(FString(What) + TEXT(" finalizes"), Final && Final->Header.RequestId == Sent.RequestId))
        {
            GrowthTestReason(*this, FString(What) + TEXT(" (commit)"), LDGrowthWire::Reason(Final->Header.Reason), Expected);
            return Final;
        }
        return nullptr;
    };
    const FLDDevTestReceipt* NoCamps = HostCommit(TEXT("Request 17: the Host's camp refresh without a camp director"),
        Command(Gen, 1, 17, ELDDevTestOp::RefreshCamps, 0), ELDGrowthReason::DataNotReady);
    TestTrue(TEXT("The refusal names the missing camps"), NoCamps && NoCamps->Detail.Contains(TEXT("camp director")));
    if (!TestTrue(TEXT("The capture reducer begins far in the future"), Mode->CaptureReducer.Begin(1000000, 500, 500, Error))) { return false; }
    Mode->MatchPhase = ELDMatchPhase::CaptureOpen;
    if (!HostCommit(TEXT("Request 18: an early capture outside development"), Command(Gen, 1, 18, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::StaleContext)) { return false; }
    TestEqual(TEXT("A refused early capture keeps the scheduled opening"), Mode->CaptureReducer.GetOpenAtMs(), int64(1000000));
    Mode->MatchPhase = ELDMatchPhase::Development;
    if (!HostCommit(TEXT("Request 19: the Host's early capture during development"), Command(Gen, 1, 19, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::None)) { return false; }
    TestTrue(TEXT("The circle opens at the commit boundary and the driver stops there"),
        Mode->CaptureReducer.GetOpenAtMs() == StepAtMs && Mode->NextCaptureDueMs() == StepAtMs && !Mode->CaptureReducer.IsOpen());
    FLDCaptureReducer Circle;
    FString CircleError;
    TestFalse(TEXT("An inactive reducer opens nothing early"), Circle.DevOpenAt(10, CircleError));
    TestTrue(TEXT("A pure reducer begins"), Circle.Begin(5000, 500, 500, CircleError));
    TestFalse(TEXT("An early opening must be earlier than the schedule"), Circle.DevOpenAt(5000, CircleError));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("An early opening is the next CloseBoundary's opening"), Circle.DevOpenAt(1200, CircleError)
        && Circle.NextDueMs() == 1200 && Circle.CloseBoundary(1200, Proposal, CircleError) && Circle.IsOpen());
    TestFalse(TEXT("An open circle opens nothing early"), Circle.DevOpenAt(1300, CircleError));

    // A closed match opens no new request, while a replay still answers.
    Mode->MatchPhase = ELDMatchPhase::Results;
    Status(TEXT("A new request after the match closed"), Economy->AdmitDevTest(Tester, Command(Gen, 1, 20, ELDDevTestOp::GrantGold, 5)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::MatchClosed);
    Status(TEXT("A replay after the match closed"), Economy->AdmitDevTest(Tester, GoldGrant), ELDAdmissionStatus::Final,
        ELDGrowthReason::None);
    TestTrue(FString::Printf(TEXT("The ledger still reconciles (%s)"), *Error), Economy->Ledger.Reconcile(Error));
    return true;
}

// #112 §8: one contender death settles the killer's full kill gold and the assisters' split share in one composite commit,
// every XP part split evenly, exactly once; malformed assister facts are refused; contributions record on the single entry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAssistSettlementTest, "Lockdown.Rules.DemoAssist.Settlement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAssistSettlementTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const FLDDemoContentParameters& Tuning = Catalog->Get().Parameters;
    TestTrue(TEXT("The approved assist window is 15 s and the assist gold share is 50%"),
        Tuning.AssistWindowMs == 15000 && Tuning.AssistGoldBp == 5000);

    // ---- Public Q/W facts of a build (#112 §10) ----
    const FLDDemoAbilityDefinition* Active = Catalog->Get().Abilities.FindByPredicate(
        [](const FLDDemoAbilityDefinition& Row) { return Row.Kind == ELDDemoAbilityKind::Active; });
    const FLDDemoAbilityDefinition* Passive = Catalog->Get().Abilities.FindByPredicate(
        [](const FLDDemoAbilityDefinition& Row) { return Row.Kind == ELDDemoAbilityKind::Passive; });
    if (!TestTrue(TEXT("The catalog has an active and a passive ability"), Active && Passive)) { return false; }
    FLDBuildProjectionInput Build;
    Build.Level = 5;
    FLDLearnedActiveValue& Held = Build.Actives.AddDefaulted_GetRef();
    Held.AbilityId = FName(*Active->Id);
    Held.Rank = uint8(Catalog->Get().Progression.MaxSkillRank);
    Held.Entry = ELDCombatEntry::W;
    FLDObservedActivesView Public;
    TestTrue(TEXT("A held W active projects its native id and rank into the W slot only"),
        ALDCombatDirector::PublicActivesOf(Build, *Catalog, Public) && Public.Abilities[0] == 0 && Public.Ranks[0] == 0
        && Public.Abilities[1] == Active->NativeId && Public.Ranks[1] == Held.Rank);
    FLDBuildProjectionInput PassiveBuild = Build;
    PassiveBuild.Actives[0].AbilityId = FName(*Passive->Id);
    TestFalse(TEXT("A passive never fills a public Q/W slot"), ALDCombatDirector::PublicActivesOf(PassiveBuild, *Catalog, Public));
    FLDBuildProjectionInput OverRank = Build;
    OverRank.Actives[0].Rank = uint8(Catalog->Get().Progression.MaxSkillRank + 1);
    TestFalse(TEXT("A rank above the approved maximum is refused"), ALDCombatDirector::PublicActivesOf(OverRank, *Catalog, Public));

    // ---- Authority fixture ----
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated assist world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* GameState = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Economy = World->SpawnActor<ALDEconomyDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Mode || !GameState || !Driver || !Economy || !Development) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = GameState;
    World->CopyGameState(Mode, GameState);
    Mode->MatchContext = {5, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Economy = Economy;
    Mode->Development = Development;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    // Sorted ids keep the expected assister order readable: A1 < A2.
    const FGuid Killer(0xA5, 0, 0, 0x10), A1(0xA5, 0, 0, 0x20), A2(0xA5, 0, 0, 0x30), Victim(0xA5, 0, 0, 0x40);
    const TArray<FGuid> Roster = {Killer, A1, A2, Victim};
    for (const FGuid& Id : Roster)
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = ELDLifePhase::Unspawned;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    const int64 StartingGold = Tuning.StartingGold;
    Economy->Match = Mode->MatchContext;
    Economy->Roster = Roster;
    Economy->Content = Catalog;
    if (!TestTrue(TEXT("The match ledger opens"), Economy->Ledger.Initialize(Mode->MatchContext.LedgerMatch, Roster, StartingGold, Error)))
    { AddError(Error); return false; }
    if (!TestTrue(TEXT("Growth opens for the roster"), Development->BeginMatch(Mode->MatchContext, Roster, Catalog, Error)))
    { AddError(Error); return false; }
    for (const FGuid& Id : Roster)
    {
        FLDCombatEntityRecord& Record = Driver->Records.Add(Id);
        Record.Kind = ELDVictimKind::Contender;
        Record.Entity = Id;
        Record.Build.Level = 1;
    }
    World->Tick(LEVELTICK_TimeOnly, .025f);

    // ---- The single contribution entry ----
    FLDBodyCombatState Life;
    Life.Victim = {ELDVictimKind::Contender, Victim, 1};
    TestTrue(TEXT("Another contender's contact records a contribution"), Driver->RecordAssistContribution(Life, A1, 1000));
    TestFalse(TEXT("A contact by the victim itself is no contribution"), Driver->RecordAssistContribution(Life, Victim, 1100));
    TestFalse(TEXT("An unowned contact is no contribution"), Driver->RecordAssistContribution(Life, FGuid(), 1100));
    TestFalse(TEXT("A contact by an entity without a contender record is no contribution"),
        Driver->RecordAssistContribution(Life, FGuid(0xA5, 0, 0, 0x99), 1100));
    TestTrue(TEXT("A later contact refreshes the same contributor"), Driver->RecordAssistContribution(Life, A1, 4000)
        && Life.AssistContributions.Num() == 1 && Life.AssistContributions[0].LastAtMs == 4000);
    FLDBodyCombatState Camp;
    Camp.Victim = {ELDVictimKind::CampMember, FGuid::NewGuid(), 1};
    TestFalse(TEXT("Camp members collect no assist contributions"), Driver->RecordAssistContribution(Camp, A1, 1000)
        || !Camp.AssistContributions.IsEmpty());
    Life.bPendingDeath = true;
    TestFalse(TEXT("A life whose death is locked takes no new contribution"), Driver->RecordAssistContribution(Life, A2, 5000));

    // ---- Settlement ----
    int64 StepAtMs = 0;
    const auto Settle = [&](const FLDCommittedDeath& Fact, FLDRewardReceipt& Out)
    {
        StepAtMs += 1;
        Error.Reset();
        if (Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) != ELDCommitReject::None)
        {
            AddError(FString::Printf(TEXT("The boundary at %lld ms did not begin."), StepAtMs));
            return false;
        }
        const bool Ran = Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::DeathsAndRewards,
                [&](const FLDGameplayWritePermit& Permit) { Out = Economy->CommitDeathReward(Fact, Permit); return true; }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!Ran) { AddError(FString::Printf(TEXT("DeathsAndRewards boundary failed: %s"), *Error)); }
        return Ran;
    };
    const auto Gold = [&](const FGuid& Id) { const LDEconomy::FAccount* A = Economy->Ledger.Account(Id); return A ? A->Gold : -1; };
    const auto XP = [&](const FGuid& Id) { const FLDProgressionState* P = Development->FindProgression(Id); return P ? P->CumulativeXP : -1; };
    const auto ContenderDeath = [&](uint32 LifeNumber, const TArray<FGuid>& Assisters)
    {
        FLDCommittedDeath Fact;
        Fact.Key = {Mode->MatchContext.LedgerMatch, {ELDVictimKind::Contender, Victim, LifeNumber}};
        Fact.VictimBody = {Fact.Key.Victim, FGuid::NewGuid()};
        Fact.VictimContender = Victim;
        Fact.FatalEffectId = FGuid(0xA5, 1, 0, LifeNumber);
        Fact.CreditOwner = Killer;
        Fact.RewardBasis.RewardDefinitionId = FName(*Catalog->Get().Character.Id);
        Fact.RewardBasis.VictimLevelAtDeath = 1;
        Fact.RewardBasis.bCreditOwnerEligibleAtDeath = true;
        Fact.RewardBasis.NoRewardReason = ELDNoRewardReason::None;
        Fact.Assisters = Assisters;
        Fact.OccurredAtMs = 1000;
        Fact.OccurredAtSeconds = 1.;
        return Fact;
    };
    LDProgression::FRewardAmounts Base;
    GrowthTestReason(*this, TEXT("The level-1 contender bounty is approved"),
        LDProgression::ContenderKillReward(Tuning, uint8(Catalog->Get().Progression.MaxLevel), 1, Base), ELDGrowthReason::None);
    const int64 AssistGold = Base.Gold * Tuning.AssistGoldBp / 10000 / 2;
    const int64 SharedXP = Base.XP / 3;
    if (!TestTrue(TEXT("The split leaves every share below the first level threshold"),
        Catalog->Get().Progression.XPThresholds.Num() >= 2 && SharedXP < Catalog->Get().Progression.XPThresholds[1]))
    { return false; }

    const FLDCommittedDeath Assisted = ContenderDeath(1, {A1, A2});
    const uint64 SerialBefore = Economy->Ledger.CommittedSerial();
    FLDRewardReceipt Receipt;
    if (!Settle(Assisted, Receipt)) { return false; }
    TestTrue(TEXT("The assisted kill is granted once"), Receipt.Outcome == ELDRewardOutcome::Granted && Receipt.Recipient == Killer);
    TestEqual(TEXT("The killer keeps the full kill gold"), Gold(Killer), StartingGold + Base.Gold);
    TestTrue(TEXT("Each assister gets half the base kill gold split evenly, floored"),
        Gold(A1) == StartingGold + AssistGold && Gold(A2) == StartingGold + AssistGold);
    TestTrue(TEXT("Killer and assisters split the XP evenly, floored"),
        XP(Killer) == SharedXP && XP(A1) == SharedXP && XP(A2) == SharedXP && XP(Victim) == 0);
    TestEqual(TEXT("The victim keeps his gold"), Gold(Victim), StartingGold);
    TestTrue(TEXT("The receipt lists both assisters with their parts in fact order"),
        Receipt.Assists.Num() == 2 && Receipt.Assists[0].Recipient == A1 && Receipt.Assists[1].Recipient == A2
        && Receipt.Assists[0].GoldDelta == AssistGold && Receipt.Assists[1].XPDelta == SharedXP
        && Receipt.GoldDelta == Base.Gold && Receipt.XPDelta == SharedXP);
    TestTrue(TEXT("One composite commit writes three consecutive single-account mints"),
        Economy->Ledger.CommittedSerial() == SerialBefore + 3);
    TestTrue(TEXT("The receipt's ledger serials are consecutive"), Receipt.bHasLedgerSerial && Receipt.Assists[0].bHasLedgerSerial
        && Receipt.Assists[0].LedgerSerial == Receipt.LedgerSerial + 1 && Receipt.Assists[1].LedgerSerial == Receipt.LedgerSerial + 2);
    TestTrue(FString::Printf(TEXT("The ledger reconciles after the composite (%s)"), *Error), Economy->Ledger.Reconcile(Error));

    FLDRewardReceipt Replay;
    if (!Settle(Assisted, Replay)) { return false; }
    TestTrue(TEXT("The same death settles only once: a replay returns the original receipt and pays nothing"),
        Replay.Outcome == ELDRewardOutcome::Granted && Replay.Assists.Num() == 2 && Gold(A1) == StartingGold + AssistGold
        && Economy->Ledger.CommittedSerial() == SerialBefore + 3);
    FLDRewardReceipt Conflict;
    if (!Settle(ContenderDeath(1, {A1}), Conflict)) { return false; }
    GrowthTestReason(*this, TEXT("Another assister set under the same death key conflicts"), Conflict.Reason, ELDGrowthReason::DeathConflict);

    const auto Refused = [&](const TCHAR* What, const FLDCommittedDeath& Fact)
    {
        FLDRewardReceipt Out;
        const uint64 Before = Economy->Ledger.CommittedSerial();
        if (!Settle(Fact, Out)) { return; }
        TestTrue(What, Out.Outcome == ELDRewardOutcome::PermanentlyRejected && Out.Reason == ELDGrowthReason::DataInvalid
            && Economy->Ledger.CommittedSerial() == Before);
    };
    Refused(TEXT("The killer is never his own assister"), ContenderDeath(2, {A1, Killer}));
    Refused(TEXT("Assisters are sorted and unique"), ContenderDeath(3, {A2, A1}));
    Refused(TEXT("The victim is never an assister"), ContenderDeath(4, {Victim}));
    Refused(TEXT("An assister must hold an account in this match"), ContenderDeath(5, {FGuid(0xA5, 0, 0, 0x99)}));
    FLDCommittedDeath Unowned = ContenderDeath(6, {A1});
    Unowned.CreditOwner.Invalidate();
    Unowned.RewardBasis.bCreditOwnerEligibleAtDeath = false;
    Unowned.RewardBasis.NoRewardReason = ELDNoRewardReason::NoOwner;
    Refused(TEXT("A death without a killer carries no assisters"), Unowned);
    FLDCommittedDeath Camp2;
    Camp2.Key = {Mode->MatchContext.LedgerMatch, {ELDVictimKind::CampMember, FGuid::NewGuid(), 1}};
    Camp2.CreditOwner = Killer;
    Camp2.RewardBasis.RewardDefinitionId = FName(*Catalog->Get().Monsters[0].Id);
    Camp2.RewardBasis.bCreditOwnerEligibleAtDeath = true;
    Camp2.RewardBasis.NoRewardReason = ELDNoRewardReason::None;
    Camp2.Assisters = {A1};
    Refused(TEXT("A camp kill only rewards the last hit"), Camp2);

    FLDRewardReceipt Plain;
    const int64 KillerXPBefore = XP(Killer);
    Unowned = ContenderDeath(7, {});
    Unowned.CreditOwner.Invalidate();
    Unowned.RewardBasis.bCreditOwnerEligibleAtDeath = false;
    Unowned.RewardBasis.NoRewardReason = ELDNoRewardReason::NoOwner;
    if (!Settle(Unowned, Plain)) { return false; }
    TestTrue(TEXT("A death without a killer pays nobody"), Plain.Outcome == ELDRewardOutcome::NoReward && XP(Killer) == KillerXPBefore
        && Gold(A1) == StartingGold + AssistGold);

    // An assister that left keeps his account open while the death crediting him awaits its retry.
    Driver->RetryRewards.Add(ContenderDeath(8, {A2}));
    TestTrue(TEXT("An owed assist keeps the assister's account from retiring"), Driver->OwesRewardTo(A2) && Driver->OwesRewardTo(Killer)
        && !Driver->OwesRewardTo(A1));
    Driver->RetryRewards.Reset();

    // D27 (#79): the 上热搜 bounty is extra bounty. The killer gets it on top of the kill gold; the assist share is
    // still half the base kill gold only (APP_SPEC: 不含额外赏金).
    {
        FLDCommittedDeath Leader = ContenderDeath(9, {A1});
        const int64 Bounty = Tuning.LeaderBountyGoldPerRefresh;
        Leader.RewardBasis.LeaderBountyGoldAtDeath = Bounty;
        const int64 KillerBefore = Gold(Killer), AssisterBefore = Gold(A1);
        FLDRewardReceipt LeaderReceipt;
        if (!Settle(Leader, LeaderReceipt)) { return false; }
        TestTrue(TEXT("A leader kill with an assister is granted"), Bounty > 0 && LeaderReceipt.Outcome == ELDRewardOutcome::Granted);
        TestEqual(TEXT("The killer gets the kill gold and the whole leader bounty"), Gold(Killer), KillerBefore + Base.Gold + Bounty);
        TestEqual(TEXT("The assister's gold leaves the leader bounty out of the assist base"), Gold(A1),
            AssisterBefore + Base.Gold * Tuning.AssistGoldBp / 10000);
    }
    return true;
}

// #147 v7 §5.1: the slotted three-choice draft (upgrade, new skill, attribute or new skill), the full-slot new-active weight,
// per-offer limits, reroll avoidance, and a replacement active starting at its basic rank.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthDraftTest, "Lockdown.Rules.DemoGrowthDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthDraftTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunDraft(*this);
}

// #147: a skill's fixed amounts grow with its owner's level, G(L) = 1 + SKILL.GrowthPerLevel x (L - 1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthSkillGrowthTest, "Lockdown.Rules.DemoGrowthSkillGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthSkillGrowthTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunSkillGrowth(*this);
}

// 112 §7: equipment upgrade in the ledger, the effective definitions and the attribute projection.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthUpgradeTest, "Lockdown.Rules.DemoGrowthUpgrade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthUpgradeTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunUpgrade(*this);
}

// 112 §6: the reroll command through admission and the Transactions commit: tokens, one per offer, receipt replay.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthRerollTest, "Lockdown.Rules.DemoGrowthReroll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthRerollTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated reroll world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Economy = World->SpawnActor<ALDEconomyDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    auto* Vendor = World->SpawnActor<ALDVendorDirector>();
    if (!Mode || !Public || !Driver || !Economy || !Development || !Vendor) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {6, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Economy = Economy;
    Mode->Development = Development;
    Mode->Vendor = Vendor;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Tester(0x112, 6, 0, 1);
    auto& Life = Mode->Lives.Add(Tester);
    Life.Kind = ELDVictimKind::Contender;
    // Dead may confirm a choice; a reroll needs no body.
    Life.Phase = ELDLifePhase::Dead;
    Life.Life = 1;
    Life.Revision = 2;
    FString Error;
    Economy->Match = Mode->MatchContext;
    Economy->Roster = {Tester};
    Economy->Content = Catalog;
    if (!TestTrue(TEXT("The match ledger opens"), Economy->Ledger.Initialize(Mode->MatchContext.LedgerMatch, Economy->Roster,
        Catalog->Get().Parameters.StartingGold, Error))) { AddError(Error); return false; }
    if (!TestTrue(TEXT("Growth opens"), Development->BeginMatch(Mode->MatchContext, Economy->Roster, Catalog, Error))) { AddError(Error); return false; }
    World->Tick(LEVELTICK_TimeOnly, .025f);
    int64 StepAtMs = 0;
    const auto RunTransactions = [&]()
    {
        StepAtMs += 1;
        Error.Reset();
        const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions,
                [&](const FLDGameplayWritePermit& Permit) { return Economy->ProcessTransactions(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!bRan) { AddError(FString::Printf(TEXT("Transactions boundary failed: %s"), *Error)); }
        return bRan;
    };
    uint64 NextRequest = 0;
    const auto MakeReroll = [&]()
    {
        const FLDProgressionView View = Development->ReadProgression(Tester);
        FLDChoiceCommand Out;
        Out.Generation = View.Generation;
        Out.Life = View.Life;
        Out.ExpectedLifePhase = View.LifePhase;
        Out.RequestId = ++NextRequest;
        Out.OfferId = View.Offer.OfferId;
        Out.OfferRevision = View.Offer.OfferRevision;
        Out.ExpectedBuildRevision = View.BuildRevision;
        Out.CatalogDigest = View.CatalogDigest;
        Out.bReroll = true;
        return Out;
    };
    const auto Final = [&](uint64 RequestId)
    {
        FLDReceiptQuery Query;
        Query.Generation = Mode->MatchContext.Generation;
        Query.Family = ELDGrowthFamily::Choice;
        Query.RequestId = RequestId;
        return Development->QueryChoiceReceipt(Tester, Query);
    };

    FLDChoiceCommand Bad = MakeReroll();
    Bad.OptionId = FGuid::NewGuid();
    TestEqual(TEXT("A reroll naming an option is refused at admission"),
        LDGrowthWire::Reason(Development->QueueChoice(Tester, Bad).Reason), ELDGrowthReason::InvalidRequest);
    NextRequest = 0;

    // Level 1 has no token.
    FLDChoiceCommand Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    FLDChoiceReceiptQueryResult Result = Final(Command.RequestId);
    TestTrue(TEXT("Without a token the reroll is refused with NoReroll and nothing changes"), Result.bHasReceipt
        && Result.Receipt.Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Result.Receipt.Header.Reason) == ELDGrowthReason::NoReroll
        && Development->ReadProgression(Tester).Offer.OfferId == Command.OfferId);

    // Level 4 earns one token: the opening offer is replaced by a new offer of the same opportunity.
    Development->Records.FindChecked(Tester).Build.Level = 4;
    const FLDProgressionView Before = Development->ReadProgression(Tester);
    TestEqual(TEXT("Level 4 holds one reroll token"), Before.RerollTokens, uint8(1));
    Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    Result = Final(Command.RequestId);
    const FLDProgressionView After = Development->ReadProgression(Tester);
    TestTrue(TEXT("The reroll is applied and names the replacing offer"), Result.bHasReceipt
        && Result.Receipt.Header.Outcome == ELDReceiptOutcome::Applied && Result.Receipt.bRerolled
        && Result.Receipt.bHasNextOffer && Result.Receipt.NextOfferId == After.Offer.OfferId && Result.Receipt.RerollTokensLeft == 0);
    TestTrue(TEXT("Same opportunity, a new offer marked rerolled, the token spent, nothing consumed or learned"),
        After.Offer.OfferId != Before.Offer.OfferId && After.Offer.OpportunityOrdinal == Before.Offer.OpportunityOrdinal
        && After.Offer.bRerolled && After.RerollTokens == 0 && After.ConsumedThrough == Before.ConsumedThrough
        && After.BuildRevision == Before.BuildRevision && After.Skills.IsEmpty());
    int32 Repeated = 0;
    for (const FLDChoiceOption& Option : After.Offer.Options)
    {
        Repeated += Before.Offer.Options.ContainsByPredicate([&Option](const FLDChoiceOption& Old)
            { return Old.DefinitionId == Option.DefinitionId && Old.Kind == Option.Kind; }) ? 1 : 0;
    }
    TestEqual(TEXT("The rerolled opening avoids the previous three cards"), Repeated, 0);
    const FLDSubmitResult Replay = Development->QueueChoice(Tester, Command);
    TestTrue(TEXT("A replay answers the original receipt without a second reroll"), Replay.Status == ELDAdmissionStatus::Final
        && Replay.bHasChoiceReceipt && Replay.ChoiceReceipt.NextOfferId == After.Offer.OfferId
        && Development->ReadProgression(Tester).Offer.OfferId == After.Offer.OfferId);

    // Level 7 earns another token, kept for later: the rerolled offer still cannot be rerolled again.
    Development->Records.FindChecked(Tester).Build.Level = 7;
    TestEqual(TEXT("Level 7 holds the second token"), Development->ReadProgression(Tester).RerollTokens, uint8(1));
    Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    Result = Final(Command.RequestId);
    TestTrue(TEXT("One reroll per offer"), Result.bHasReceipt && LDGrowthWire::Reason(Result.Receipt.Header.Reason)
        == ELDGrowthReason::OfferAlreadyRerolled && Development->ReadProgression(Tester).RerollTokens == 1);
    // A stale offer revision is refused like any choice.
    FLDChoiceCommand Stale = MakeReroll();
    Stale.OfferRevision += 1;
    Development->QueueChoice(Tester, Stale);
    if (!RunTransactions()) { return false; }
    Result = Final(Stale.RequestId);
    TestTrue(TEXT("A reroll with a stale guard is refused"), Result.bHasReceipt
        && LDGrowthWire::Reason(Result.Receipt.Header.Reason) == ELDGrowthReason::OfferGuardStale);
    return true;
}

#endif
