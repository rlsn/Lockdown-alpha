#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Dom/JsonObject.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDEconomyLedger.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
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
    Choice.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_arc_bolt"));
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
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_guard_pulse")); });
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
    // Approved values (CONTENT_AND_BALANCE §5/§6.1): SHOP.StartingGold 150, item_iron_plate 310 / 201,
    // a level 10 contender kill pays 170 gold and a monster_scavenger 30.
    const int64 StartingGold = 150;
    const int64 PlatePrice = 310;
    const int64 PlateSellGold = 201;
    const FName Plate(TEXT("item_iron_plate"));
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
    bGood &= Test.TestEqual(TEXT("Initial gold is 3 x 150"), Ledger.TotalGold(), int64(450));
    bGood &= Test.TestEqual(TEXT("The initial gold total is recorded for reconciliation"), Ledger.State().InitialGoldTotal, int64(450));
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
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 230, 2, TEXT("After the 80 gold death reward"));
    bGood &= Test.TestEqual(TEXT("The reward is committed as serial 1"), Ledger.CommittedSerial(), uint64(1));
    bGood &= Test.TestEqual(TEXT("The next commit will be serial 2"), Ledger.NextCommitSerial(), uint64(2));
    bGood &= Test.TestEqual(TEXT("The reward is classified as minted gold"), Ledger.State().Minted, int64(80));
    bGood &= Test.TestEqual(TEXT("A reward leaves the inventory revision"), Ledger.InventoryRevision(P1), uint64(1));
    bGood &= GrowthTestReconciles(Test, Ledger, TEXT("After the reward"));

    bGood &= GrowthTestRefuse(Test, Ledger, GrowthTestBuy(50, P1, 2, 1, Plate, 0, PlatePrice), LDEconomy::EPrepareError::InsufficientGold,
        TEXT("Buying the 310 gold plate with 230 gold"));

    for (uint32 Key = 2; Key <= 6; ++Key)
    {
        if (!GrowthTestCommit(Test, Ledger, GrowthTestReward(Key, P1, GrowthTestAccountRevision(Ledger, P1), 170),
            TEXT("A level 10 kill reward")))
        {
            return false;
        }
    }
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 1080, 7, TEXT("After five 170 gold rewards"));

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
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 770, 8, TEXT("After buying the first plate"));
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
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 460, 9, TEXT("After buying the second plate"));
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
    bGood &= GrowthTestExpectAccount(Test, Ledger, P1, 661, 10, TEXT("After selling for 201"));
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
    bGood &= Test.TestEqual(TEXT("Total gold after compaction"), Ledger.TotalGold(), int64(2581));
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
    bGood &= GrowthTestExpectAccount(Test, Ledger, P2, 1830, 57, TEXT("The filler account at the end"));
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
    const int64 StartingGold = 150;
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
    bGood &= Test.TestTrue(TEXT("The evidence names schema 3, its match and the approved starting gold"),
        Root->GetIntegerField(TEXT("schemaVersion")) == 3
        && Root->GetStringField(TEXT("match")) == Match.ToString(EGuidFormats::DigitsWithHyphensLower)
        && Root->GetStringField(TEXT("startingGold")) == TEXT("150"));
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
    bApproved &= Test.TestTrue(TEXT("Fixture carries MaxLevel 10, MaxChoices 9, MaxSkillRank 3, two active and two equipment slots"),
        Curve.MaxLevel == 10 && Curve.MaxChoices == 9 && Curve.MaxSkillRank == 3 && Curve.ActiveSlots == 2 && Curve.EquipmentSlots == 2);
    bApproved &= Test.TestTrue(TEXT("Fixture carries REWARD.PlayerXP 100 + 20/level and REWARD.PlayerGold 80 + 10/level"),
        Tuning.PlayerXPBase == 100 && Tuning.PlayerXPPerLevel == 20 && Tuning.PlayerGoldBase == 80 && Tuning.PlayerGoldPerLevel == 10);
    bApproved &= Test.TestTrue(TEXT("Fixture carries CHOICE.BaseWeight 1, SHOP.StartingGold 150 and SHOP.SellRatio .65"),
        Tuning.ChoiceBaseWeight == 1 && Tuning.StartingGold == 150 && Tuning.SellRatioBp == 6500);
    bApproved &= Test.TestTrue(TEXT("Fixture prices item_iron_plate at 310 / 201 as the ledger test assumes"),
        OutCatalog->FindEquipment(TEXT("item_iron_plate"), IronPlate, Missing) && IronPlate->BuyPrice == 310 && IronPlate->SellGold == 201);
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
    const ELDGrowthReason Outcome = LDProgression::PlanExperience(Curve, CurrentXP, CurrentLevel, uint8(CurrentLevel - 1), Grant, Change);
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

    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 80, 2, 1, TEXT("80 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 1440, 10, 9, TEXT("1440 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 1440, 10, 1000, 10, 9, TEXT("1000 XP at level 10"));
    LDProgression::FExperienceChange Change;
    bGood &= GrowthTestReason(Test, TEXT("1 XP past the int64 bound"), LDProgression::PlanExperience(Curve, MAX_int64, 10, 9, 1, Change),
        ELDGrowthReason::ArithmeticOverflow);
    bGood &= Test.TestTrue(TEXT("An overflowing plan carries no partial result"), Change.NewXP == 0 && Change.LevelAfter == 0);
    bGood &= GrowthTestReason(Test, TEXT("80 XP recorded as level 1"), LDProgression::PlanExperience(Curve, 80, 1, 0, 20, Change),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 2 without its earned opportunity"),
        LDProgression::PlanExperience(Curve, 80, 2, 0, 20, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("A negative XP grant"), LDProgression::PlanExperience(Curve, 0, 1, 0, -1, Change),
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
    bGood &= Test.TestEqual(TEXT("An empty build may learn three actives"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnActive), 3);
    bGood &= Test.TestEqual(TEXT("An empty build may learn three passives"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnPassive), 3);
    bGood &= Test.TestEqual(TEXT("An empty build may take all five attribute cards"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::Attribute), 5);
    const TPair<ELDChoiceOptionKind, const TCHAR*> ApprovedPool[] = {
        {ELDChoiceOptionKind::LearnActive, TEXT("active_arc_bolt")},
        {ELDChoiceOptionKind::LearnActive, TEXT("active_binding_snare")},
        {ELDChoiceOptionKind::LearnActive, TEXT("active_guard_pulse")},
        {ELDChoiceOptionKind::Attribute, TEXT("attr_focus")},
        {ELDChoiceOptionKind::Attribute, TEXT("attr_might")},
        {ELDChoiceOptionKind::Attribute, TEXT("attr_reach")},
        {ELDChoiceOptionKind::Attribute, TEXT("attr_resolve")},
        {ELDChoiceOptionKind::Attribute, TEXT("attr_vitality")},
        {ELDChoiceOptionKind::LearnPassive, TEXT("passive_battle_rhythm")},
        {ELDChoiceOptionKind::LearnPassive, TEXT("passive_precision")},
        {ELDChoiceOptionKind::LearnPassive, TEXT("passive_second_wind")}};
    if (!Test.TestEqual(TEXT("The empty build's legal pool has 3 + 3 + 5 options"), Pool.Num(), int32(UE_ARRAY_COUNT(ApprovedPool))))
    {
        return false;
    }
    TSet<FName> PoolIds;
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        const LDProgression::FLegalOption& Row = Pool[Index];
        PoolIds.Add(Row.DefinitionId);
        bGood &= Test.TestTrue(FString::Printf(TEXT("Pool row %d is %s from rank 0 to 1"), Index, ApprovedPool[Index].Value),
            Row.Kind == ApprovedPool[Index].Key && Row.DefinitionId.ToString().Equals(ApprovedPool[Index].Value, ESearchCase::CaseSensitive)
            && Row.FromRank == 0 && Row.ToRank == 1);
    }
    bGood &= Test.TestTrue(TEXT("The pool is sorted by definition id, then kind"), GrowthTestPoolSorted(Pool));
    bGood &= Test.TestEqual(TEXT("No definition appears twice in the pool"), PoolIds.Num(), Pool.Num());

    const int64 Weight = Approved.Parameters.ChoiceBaseWeight;
    const FGuid MatchSecret(0x5EC2E700, 1, 2, 3);
    const FGuid Contender(0x0000A001, 0, 0, 1);
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, Contender, 1);
    bGood &= Test.TestTrue(TEXT("Retrying opportunity 1 reuses its seed"), Seed == LDProgression::OfferSeed(MatchSecret, Contender, 1));
    bGood &= Test.TestTrue(TEXT("Opportunity 2 draws from another seed"), Seed != LDProgression::OfferSeed(MatchSecret, Contender, 2));
    bGood &= Test.TestTrue(TEXT("Another contender's opportunity 1 draws from another seed"),
        Seed != LDProgression::OfferSeed(MatchSecret, FGuid(0x0000A002, 0, 0, 2), 1));
    TArray<LDProgression::FLegalOption> Offer;
    if (!GrowthTestReason(Test, TEXT("Drawing an offer"), LDProgression::DrawThree(Pool, Weight, Seed, Offer), ELDGrowthReason::None)
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
    bGood &= GrowthTestReason(Test, TEXT("Redrawing with the same seed"), LDProgression::DrawThree(Pool, Weight, Seed, Redrawn),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The same seed draws the same options in the same order"), GrowthTestSameOptions(Offer, Redrawn));
    TSet<FName> Reachable;
    for (uint64 TrialSeed = 1; TrialSeed <= 64; ++TrialSeed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("Drawing with seed %llu"), TrialSeed),
            LDProgression::DrawThree(Pool, Weight, TrialSeed, Drawn), ELDGrowthReason::None);
        for (const LDProgression::FLegalOption& DrawnOption : Drawn)
        {
            Reachable.Add(DrawnOption.DefinitionId);
        }
    }
    bGood &= Test.TestEqual(TEXT("Every legal option is drawn by some of 64 seeds"), Reachable.Num(), Pool.Num());
    TArray<LDProgression::FLegalOption> Refused;
    bGood &= GrowthTestReason(Test, TEXT("Drawing with weight 0"), LDProgression::DrawThree(Pool, 0, Seed, Refused),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Drawing with a negative weight"), LDProgression::DrawThree(Pool, -1, Seed, Refused),
        ELDGrowthReason::DataInvalid);
    const TArray<LDProgression::FLegalOption> TwoRows = {Pool[0], Pool[1]};
    bGood &= GrowthTestReason(Test, TEXT("Drawing three from two legal options"), LDProgression::DrawThree(TwoRows, Weight, Seed, Refused),
        ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("Refused draws offer nothing"), Refused.Num(), 0);
    const TArray<LDProgression::FLegalOption> ThreeRows = {Pool[0], Pool[1], Pool[2]};
    TArray<LDProgression::FLegalOption> AllThree;
    bGood &= GrowthTestReason(Test, TEXT("Drawing three from exactly three legal options"),
        LDProgression::DrawThree(ThreeRows, Weight, Seed, AllThree), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Exactly three legal options are all offered"), AllThree.Num() == 3
        && GrowthTestPoolHas(AllThree, ThreeRows[0].Kind, ThreeRows[0].DefinitionId, 0, 1)
        && GrowthTestPoolHas(AllThree, ThreeRows[1].Kind, ThreeRows[1].DefinitionId, 0, 1)
        && GrowthTestPoolHas(AllThree, ThreeRows[2].Kind, ThreeRows[2].DefinitionId, 0, 1));

    const FName ArcBolt(TEXT("active_arc_bolt"));
    const FName Snare(TEXT("active_binding_snare"));
    const FName GuardPulse(TEXT("active_guard_pulse"));
    FLDGrowthBuild Build;
    LDProgression::FAppliedOption Applied;

    const FLDChoiceOption LearnArc = GrowthTestOption(ELDChoiceOptionKind::LearnActive, ArcBolt, 0, 1);
    bGood &= Test.TestTrue(TEXT("Learning a first active is legal on an empty build"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnArc));
    if (!GrowthTestApply(Test, *Catalog, Build, LearnArc, nullptr, ELDGrowthReason::None, Applied, TEXT("Learning the first active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, ArcBolt, 1, true, 0, TEXT("The first active"));
    bGood &= Test.TestTrue(TEXT("The first active lands in Q (slot 0) at rank 1 and replaces nothing"), Applied.Slot == 0
        && !Applied.bReplaced && Applied.NewRank == 1 && Applied.Kind == ELDChoiceOptionKind::LearnActive
        && Applied.DefinitionId == ArcBolt);
    bGood &= Test.TestFalse(TEXT("The applied learn option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnArc));
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnArc, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Applying the first active again"));

    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnActive, Snare, 0, 1), nullptr,
        ELDGrowthReason::None, Applied, TEXT("Learning the second active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, Snare, 1, true, 1, TEXT("The second active"));
    bGood &= Test.TestEqual(TEXT("The second active lands in W (slot 1)"), int32(Applied.Slot), 1);

    const FLDChoiceOption UpgradeArc = GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, ArcBolt, 1, 2);
    if (!GrowthTestApply(Test, *Catalog, Build, UpgradeArc, nullptr, ELDGrowthReason::None, Applied, TEXT("Upgrading Q to rank 2")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, ArcBolt, 2, true, 0, TEXT("The upgraded Q active"));
    bGood &= Test.TestFalse(TEXT("The applied upgrade option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, UpgradeArc));

    const FLDChoiceOption LearnGuard = GrowthTestOption(ELDChoiceOptionKind::LearnActive, GuardPulse, 0, 1);
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnGuard, nullptr, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("A third active without a replacement"));
    LDProgression::FReplacement Replacement;
    Replacement.Slot = 0;
    Replacement.OldSkillId = ArcBolt;
    Replacement.OldRank = 1;
    Replacement.bConfirmed = true;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnGuard, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming rank 1 of the rank 2 Q active"));
    Replacement.OldRank = 2;
    Replacement.bConfirmed = false;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnGuard, &Replacement, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("An unconfirmed replacement"));
    Replacement.bConfirmed = true;
    Replacement.Slot = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnGuard, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming the wrong slot"));
    Replacement.Slot = 0;
    if (!GrowthTestApply(Test, *Catalog, Build, LearnGuard, &Replacement, ELDGrowthReason::None, Applied,
        TEXT("The confirmed replacement of Q")))
    {
        return false;
    }
    bGood &= Test.TestNull(TEXT("The replaced active and its rank are forgotten"), Build.FindSkill(ArcBolt));
    bGood &= GrowthTestExpectSkill(Test, Build, GuardPulse, 1, true, 0, TEXT("The replacing active"));
    bGood &= GrowthTestExpectSkill(Test, Build, Snare, 1, true, 1, TEXT("The untouched W active"));
    bGood &= Test.TestTrue(TEXT("The replacement reports the slot and the forgotten skill and rank"), Applied.bReplaced && Applied.Slot == 0
        && Applied.ReplacedId == ArcBolt && Applied.ReplacedRank == 2 && Applied.NewRank == 1 && Applied.DefinitionId == GuardPulse);
    TArray<LDProgression::FLegalOption> AfterReplacement;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after the replacement"),
        LDProgression::LegalPool(*Catalog, Build, AfterReplacement), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("A forgotten active can be learned again from rank 1"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::LearnActive, ArcBolt, 0, 1));
    bGood &= Test.TestTrue(TEXT("Held actives offer their next rank"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, Snare, 1, 2)
        && GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, GuardPulse, 1, 2));
    bGood &= Test.TestTrue(TEXT("The pool stays sorted after a replacement"), GrowthTestPoolSorted(AfterReplacement));

    // A replacement that would be valid for Q is still refused by every option that is not a slot-bound new active.
    Replacement.OldSkillId = GuardPulse;
    Replacement.OldRank = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build,
        GrowthTestOption(ELDChoiceOptionKind::LearnPassive, FName(TEXT("passive_precision")), 0, 1), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to a passive"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, FName(TEXT("attr_might")), 0, 1),
        &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an attribute card"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, Snare, 1, 2), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an upgrade"));
    FLDGrowthBuild FreeSlots;
    bGood &= GrowthTestApply(Test, *Catalog, FreeSlots, LearnArc, &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied,
        TEXT("A replacement while an active slot is free"));

    for (uint8 FromRank = 1; FromRank < 3; ++FromRank)
    {
        const uint8 ToRank = uint8(FromRank + 1);
        if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, Snare, FromRank, ToRank), nullptr,
            ELDGrowthReason::None, Applied, *FString::Printf(TEXT("Upgrading W to rank %d"), ToRank)))
        {
            return false;
        }
        bGood &= Test.TestEqual(TEXT("An upgrade keeps the active in its slot"), int32(Applied.Slot), 1);
    }
    bGood &= GrowthTestExpectSkill(Test, Build, Snare, 3, true, 1, TEXT("The rank 3 W active"));
    TArray<LDProgression::FLegalOption> MaxedPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool with a rank 3 active"), LDProgression::LegalPool(*Catalog, Build, MaxedPool),
        ELDGrowthReason::None);
    bGood &= Test.TestFalse(TEXT("A rank 3 active is absent from the legal pool"), GrowthTestPoolMentions(MaxedPool, Snare));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, Snare, 3, 4), nullptr,
        ELDGrowthReason::InvalidOption, Applied, TEXT("Upgrading past rank 3"));

    for (const TCHAR* PassiveText : {TEXT("passive_battle_rhythm"), TEXT("passive_precision"), TEXT("passive_second_wind")})
    {
        const FName PassiveId(PassiveText);
        if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnPassive, PassiveId, 0, 1), nullptr,
            ELDGrowthReason::None, Applied, PassiveText))
        {
            return false;
        }
        bGood &= GrowthTestExpectSkill(Test, Build, PassiveId, 1, false, 0, TEXT("A passive learned with both active slots held"));
    }
    bGood &= Test.TestEqual(TEXT("Q still holds the guard pulse after three passives"), GrowthTestActiveIn(Build, 0), GuardPulse);
    bGood &= Test.TestEqual(TEXT("W still holds the binding snare after three passives"), GrowthTestActiveIn(Build, 1), Snare);
    bGood &= Test.TestEqual(TEXT("Two actives and three passives are learned"), Build.Skills.Num(), 5);

    const FName Might(TEXT("attr_might"));
    const FLDChoiceOption FirstMight = GrowthTestOption(ELDChoiceOptionKind::Attribute, Might, 0, 1);
    if (!GrowthTestApply(Test, *Catalog, Build, FirstMight, nullptr, ELDGrowthReason::None, Applied, TEXT("The first attr_might card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_might count after one card"), GrowthTestCardCount(Build, Might), 1);
    bGood &= Test.TestEqual(TEXT("The first card reports its new count"), int32(Applied.NewRank), 1);
    bGood &= Test.TestFalse(TEXT("The applied card option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, FirstMight));
    TArray<LDProgression::FLegalOption> CardPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after one card"), LDProgression::LegalPool(*Catalog, Build, CardPool),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The pool offers attr_might from count 1 to 2"),
        GrowthTestPoolHas(CardPool, ELDChoiceOptionKind::Attribute, Might, 1, 2));
    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, Might, 1, 2), nullptr,
        ELDGrowthReason::None, Applied, TEXT("The second attr_might card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_might count after two cards"), GrowthTestCardCount(Build, Might), 2);
    bGood &= Test.TestEqual(TEXT("The second card reports its new count"), int32(Applied.NewRank), 2);
    bGood &= GrowthTestApply(Test, *Catalog, Build, FirstMight, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Re-applying the stale first card"));
    bGood &= Test.TestEqual(TEXT("Other attribute cards are untouched"), GrowthTestCardCount(Build, FName(TEXT("attr_vitality"))), 0);
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
            !Development->DevQueueOffer(Contender, {FName(TEXT("active_arc_bolt")), FName(TEXT("attr_might")), FName(TEXT("attr_focus"))},
                QueueError) && QueueError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("DevGrantReward is refused without the switch and issues no ticket"),
            Economy->DevGrantReward(Contender, 80, 50, GrantError) == 0 && GrantError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("No ticket exists to poll after a refused grant"),
            Economy->DevGrantStatus(1, Detail) == ELDDevGrantStatus::Unknown);
    }

    const FName Bolt(TEXT("active_arc_bolt"));
    const FName Snare(TEXT("active_binding_snare"));
    const FName Pulse(TEXT("active_guard_pulse"));
    const FName Precision(TEXT("passive_precision"));
    const FName Might(TEXT("attr_might"));
    const FName Focus(TEXT("attr_focus"));
    const FName Reach(TEXT("attr_reach"));
    FLDGrowthBuild Fresh;
    TArray<LDProgression::FLegalOption> Resolved;
    bGood &= Test.TestTrue(FString::Printf(TEXT("Three legal cards resolve in their given order with kinds from the build (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Fresh, {Bolt, Precision, Might}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].DefinitionId == Bolt && Resolved[0].Kind == ELDChoiceOptionKind::LearnActive && Resolved[0].ToRank == 1
        && Resolved[1].DefinitionId == Precision && Resolved[1].Kind == ELDChoiceOptionKind::LearnPassive
        && Resolved[2].DefinitionId == Might && Resolved[2].Kind == ELDChoiceOptionKind::Attribute && Resolved[2].ToRank == 1);
    FLDGrowthBuild Learned;
    Learned.Level = 6;
    Learned.Skills = {{Bolt, 2, true, 0}, {Pulse, 3, true, 1}};
    Learned.Cards = {{Might, 2}};
    bGood &= Test.TestTrue(FString::Printf(TEXT("A learned skill resolves to its next rank and a held card to one more (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, {Bolt, Might, Snare}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].Kind == ELDChoiceOptionKind::UpgradeActive && Resolved[0].FromRank == 2 && Resolved[0].ToRank == 3
        && Resolved[1].Kind == ELDChoiceOptionKind::Attribute && Resolved[1].FromRank == 2 && Resolved[1].ToRank == 3
        && Resolved[2].Kind == ELDChoiceOptionKind::LearnActive);
    const FGrowthTestPreset BadPresets[] = {
        {TEXT("two cards"), {Bolt, Might}},
        {TEXT("four cards"), {Bolt, Might, Focus, Reach}},
        {TEXT("a repeated card"), {Bolt, Might, Bolt}},
        {TEXT("an unknown definition"), {Bolt, Might, FName(TEXT("active_unknown"))}},
        {TEXT("NAME_None"), {Bolt, Might, FName()}},
        {TEXT("a skill at its rank cap"), {Pulse, Might, Focus}}};
    for (const FGrowthTestPreset& Bad : BadPresets)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("A preset with %s is refused with a reason and no cards"), Bad.What),
            !LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, Bad.Cards, Resolved, Error) && Resolved.IsEmpty() && !Error.IsEmpty());
    }

    TArray<LDProgression::FLegalOption> Pool;
    TArray<LDProgression::FLegalOption> Ordinary;
    const int64 Weight = Catalog->Get().Parameters.ChoiceBaseWeight;
    const uint64 Seed = LDProgression::OfferSeed(FGuid(1, 2, 3, 4), FGuid(0xE3, 0, 0, 1), 1);
    bGood &= GrowthTestReason(Test, TEXT("The fresh build's legal pool"), LDProgression::LegalPool(*Catalog, Fresh, Pool), ELDGrowthReason::None);
    bGood &= GrowthTestReason(Test, TEXT("The ordinary seeded draw"), LDProgression::DrawThree(Pool, Weight, Seed, Ordinary),
        ELDGrowthReason::None);
    TArray<TArray<FName>> Queue = {{Snare, Focus, Reach}, {Bolt, FName(TEXT("active_unknown")), Might}, {Pulse, Precision, Might}};
    TArray<LDProgression::FLegalOption> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("The first head draw takes the oldest preset, in its order"),
        LDDevGrowthFixture::PickHead(Pool, Weight, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && PresetError.IsEmpty() && Picked.Num() == 3 && Picked[0].DefinitionId == Snare && Picked[1].DefinitionId == Focus
        && Picked[2].DefinitionId == Reach && Queue.Num() == 2);
    bGood &= Test.TestTrue(TEXT("A preset that no longer resolves is consumed with an explicit error and the ordinary draw runs"),
        LDDevGrowthFixture::PickHead(Pool, Weight, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && !PresetError.IsEmpty() && GrowthTestSameOptions(Picked, Ordinary) && Queue.Num() == 1);
    bGood &= Test.TestTrue(TEXT("The next head draw takes the next preset"),
        LDDevGrowthFixture::PickHead(Pool, Weight, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && Picked[0].DefinitionId == Pulse && Picked[1].DefinitionId == Precision && Picked[2].DefinitionId == Might && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("With no preset left the head is the ordinary seeded draw: a preset shapes one offer only"),
        LDDevGrowthFixture::PickHead(Pool, Weight, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
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

bool GrowthTestRunPrivacy(FAutomationTestBase& Test)
{
    // Any field added to the private growth views must be added here deliberately (and told to UI).
    bool bGood = GrowthTestExactFields(Test, FLDGrowthOwnerView::StaticStruct(),
        {TEXT("Generation"), TEXT("PublishRevision"), TEXT("Progression"), TEXT("Equipment"), TEXT("Shop")});
    bGood &= GrowthTestExactFields(Test, FLDProgressionView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Life"), TEXT("LifePhase"), TEXT("bCanChoose"), TEXT("Level"), TEXT("MaxLevel"),
         TEXT("CumulativeXP"), TEXT("LevelThresholdXP"), TEXT("bHasNextThreshold"), TEXT("NextThresholdXP"),
         TEXT("ProgressionRevision"), TEXT("BuildRevision"), TEXT("EarnedThrough"), TEXT("ConsumedThrough"),
         TEXT("PendingChoiceCount"), TEXT("bHasOffer"), TEXT("Offer"), TEXT("Skills"), TEXT("AttributeCards"), TEXT("CatalogDigest")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOfferView::StaticStruct(),
        {TEXT("OfferId"), TEXT("OpportunityOrdinal"), TEXT("OfferRevision"), TEXT("GuardBuildRevision"), TEXT("Options")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOption::StaticStruct(),
        {TEXT("OptionId"), TEXT("Kind"), TEXT("DefinitionId"), TEXT("FromRank"), TEXT("ToRank")});
    bGood &= GrowthTestExactFields(Test, FLDLearnedSkillView::StaticStruct(),
        {TEXT("DefinitionId"), TEXT("bActive"), TEXT("Rank"), TEXT("Slot")});
    bGood &= GrowthTestExactFields(Test, FLDAttributeCardView::StaticStruct(), {TEXT("DefinitionId"), TEXT("Count")});
    bGood &= GrowthTestExactFields(Test, FLDEquipmentView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Gold"), TEXT("AccountRevision"), TEXT("InventoryRevision"), TEXT("Slots")});
    bGood &= GrowthTestExactFields(Test, FLDEquipmentSlotView::StaticStruct(),
        {TEXT("SlotIndex"), TEXT("bOccupied"), TEXT("Instance"), TEXT("IssueSerial"), TEXT("DefinitionId"), TEXT("ItemRevision")});
    bGood &= GrowthTestExactFields(Test, FLDShopOpenResult::StaticStruct(),
        {TEXT("Generation"), TEXT("Life"), TEXT("ShopId"), TEXT("OpenStatus"), TEXT("Reason"), TEXT("bHasView"), TEXT("View")});
    bGood &= GrowthTestExactFields(Test, FLDShopView::StaticStruct(),
        {TEXT("ShopId"), TEXT("SessionId"), TEXT("Life"), TEXT("QuoteRevision"), TEXT("CatalogDigest"), TEXT("WorldMapId"),
         TEXT("WorldLayoutRevision"), TEXT("WorldGeometryRevision"), TEXT("Items"), TEXT("Sellable")});
    bGood &= GrowthTestExactFields(Test, FLDShopItemView::StaticStruct(),
        {TEXT("DefinitionId"), TEXT("BuyPrice"), TEXT("SellGold"), TEXT("bHasActive"), TEXT("bInheritsCooldown"), TEXT("InheritedReadyAtMs")});
    bGood &= GrowthTestExactFields(Test, FLDShopSellView::StaticStruct(),
        {TEXT("SlotIndex"), TEXT("Instance"), TEXT("ItemRevision"), TEXT("DefinitionId"), TEXT("SellPrice")});
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthPrivacySchemaTest, "Lockdown.Rules.DemoGrowthPrivacySchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthPrivacySchemaTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunPrivacy(*this);
}

#endif
