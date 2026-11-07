#include "Inventory/LDVendorDirector.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Misc/ScopeExit.h"
#include "Region/LDMapRegistry.h"
#include "Serialization/MemoryWriter.h"
#include "World/LDWorldQueryTypes.h"

namespace
{
const FLDDemoEquipmentDefinition* VendorFindEquipment(const FLDDemoContentSnapshot& Catalog, FName Id)
{
    if (Id.IsNone())
    {
        return nullptr;
    }
    return Catalog.Get().Equipment.FindByPredicate([Id](const FLDDemoEquipmentDefinition& Row) { return FName(*Row.Id) == Id; });
}

// Public shop and world identities of an open intent; false when its shape is invalid.
bool VendorDecodeOpen(const FLDShopOpenCommand& Command, FName& OutShopId, FLDWorldRevision& OutWorld)
{
    OutWorld = FLDWorldRevision();
    OutWorld.LayoutRevision = Command.WorldLayoutRevision;
    OutWorld.GeometryRevision = Command.WorldGeometryRevision;
    const bool bShopDecoded = Command.ShopId.Decode(OutShopId);
    const bool bMapDecoded = Command.WorldMapId.Decode(OutWorld.MapId);
    return Command.Generation != 0 && Command.Life != 0 && bShopDecoded && bMapDecoded && OutWorld.IsValid();
}

bool VendorSameOpen(const FLDShopOpenCommand& A, const FLDShopOpenCommand& B)
{
    return A.Generation == B.Generation && A.Life == B.Life && A.ShopId == B.ShopId && A.WorldMapId == B.WorldMapId
        && A.WorldLayoutRevision == B.WorldLayoutRevision && A.WorldGeometryRevision == B.WorldGeometryRevision;
}

// One object per command: Buy names a catalog definition at its shown price, Sell one exact instance revision.
bool VendorCommandShape(const FLDShopCommand& Command)
{
    FName MapId;
    if (Command.Generation == 0 || Command.Life == 0 || Command.RequestId == 0 || !Command.SessionId.IsValid()
        || Command.QuoteRevision == 0 || !Command.WorldMapId.Decode(MapId) || Command.WorldLayoutRevision == 0
        || Command.WorldGeometryRevision == 0 || Command.ExpectedAccountRevision == 0
        || Command.ExpectedInventoryRevision == 0 || Command.ExpectedBuildRevision == 0
        || !LDGrowthWire::IsKnownShopKind(Command.Kind))
    {
        return false;
    }
    if (Command.Kind == ELDShopCommandKind::Buy)
    {
        FName DefinitionId;
        return Command.DefinitionId.Decode(DefinitionId) && Command.BuyPrice > 0 && !Command.Instance.IsValid()
            && Command.ExpectedItemRevision == 0 && Command.SellPrice == 0 && Command.UpgradePrice == 0;
    }
    if (Command.Kind == ELDShopCommandKind::Upgrade)
    {
        return Command.Instance.IsValid() && Command.ExpectedItemRevision > 0 && Command.UpgradePrice > 0
            && Command.SellPrice == 0 && Command.DefinitionId == FLDDefinitionWire() && Command.BuyPrice == 0;
    }
    return Command.Instance.IsValid() && Command.ExpectedItemRevision > 0 && Command.SellPrice >= 0
        && Command.DefinitionId == FLDDefinitionWire() && Command.BuyPrice == 0 && Command.UpgradePrice == 0;
}

// Canonical business record of one shop request: the ledger key and terms of exactly this contender request.
void VendorCanonicalCommand(const FGuid& LedgerMatch, const FGuid& ContenderId, const FLDShopCommand& Command,
    TArray<uint8>& Out)
{
    Out.Reset();
    FMemoryWriter Writer(Out);
    FString Tag = TEXT("shop-command");
    FGuid MatchId = LedgerMatch;
    FGuid Contender = ContenderId;
    FLDShopCommand Copy = Command;
    uint8 Kind = uint8(Copy.Kind);
    Writer << Tag << MatchId << Contender << Copy.RequestId << Copy.Generation << Copy.Life << Kind << Copy.SessionId
        << Copy.QuoteRevision;
    Writer.Serialize(Copy.CatalogDigest.Bytes, sizeof(Copy.CatalogDigest.Bytes));
    Writer.Serialize(Copy.WorldMapId.Bytes, sizeof(Copy.WorldMapId.Bytes));
    Writer << Copy.WorldLayoutRevision << Copy.WorldGeometryRevision << Copy.ExpectedAccountRevision
        << Copy.ExpectedInventoryRevision << Copy.ExpectedBuildRevision;
    Writer.Serialize(Copy.DefinitionId.Bytes, sizeof(Copy.DefinitionId.Bytes));
    Writer << Copy.BuyPrice << Copy.Instance << Copy.ExpectedItemRevision << Copy.SellPrice;
    if (Copy.Kind == ELDShopCommandKind::Upgrade)
    {
        Writer << Copy.UpgradePrice;
    }
}

ELDGrowthReason VendorEligibilityReason(const FLDShopEligibilityResult& Eligibility)
{
    if (Eligibility.CommitFailure != ELDCommitReject::None)
    {
        return ELDGrowthReason::StaleContext;
    }
    if (Eligibility.Succeeded)
    {
        return Eligibility.WithinReach ? ELDGrowthReason::None : ELDGrowthReason::OutsideRange;
    }
    switch (Eligibility.WorldFailure)
    {
    case ELDWorldQueryFailure::StaleWorld: return ELDGrowthReason::StaleWorld;
    case ELDWorldQueryFailure::NotGrounded: return ELDGrowthReason::NotGrounded;
    case ELDWorldQueryFailure::InvalidSurface:
    case ELDWorldQueryFailure::WrongSurface: return ELDGrowthReason::WrongSurface;
    case ELDWorldQueryFailure::InvalidPosition:
    case ELDWorldQueryFailure::InvalidCapsule: return ELDGrowthReason::InvalidGround;
    // A failed query without a typed world failure is a broken World contract, not an approach error.
    case ELDWorldQueryFailure::MissingWorld:
    case ELDWorldQueryFailure::None: return ELDGrowthReason::DataNotReady;
    default: return ELDGrowthReason::InvalidApproachRegion;
    }
}

ELDGrowthReason VendorLedgerReason(LDEconomy::EPrepareError Error)
{
    switch (Error)
    {
    case LDEconomy::EPrepareError::InsufficientGold: return ELDGrowthReason::InsufficientGold;
    case LDEconomy::EPrepareError::AccountStale: return ELDGrowthReason::AccountStale;
    case LDEconomy::EPrepareError::InventoryStale: return ELDGrowthReason::InventoryStale;
    case LDEconomy::EPrepareError::EquipmentFull:
    case LDEconomy::EPrepareError::SlotOccupied: return ELDGrowthReason::EquipmentFull;
    case LDEconomy::EPrepareError::UnknownItem:
    case LDEconomy::EPrepareError::NotOwner: return ELDGrowthReason::ItemNotOwned;
    case LDEconomy::EPrepareError::ItemStale: return ELDGrowthReason::ItemStale;
    case LDEconomy::EPrepareError::Overflow: return ELDGrowthReason::ArithmeticOverflow;
    case LDEconomy::EPrepareError::JournalFull: return ELDGrowthReason::PrepareUnavailable;
    case LDEconomy::EPrepareError::DuplicateBusinessKey:
    case LDEconomy::EPrepareError::BusinessKeyConflict: return ELDGrowthReason::RequestConflict;
    case LDEconomy::EPrepareError::RetiredAccount: return ELDGrowthReason::LeftMatch;
    case LDEconomy::EPrepareError::UpgradeUsed: return ELDGrowthReason::UpgradeUsed;
    case LDEconomy::EPrepareError::AlreadyUpgraded: return ELDGrowthReason::AlreadyUpgraded;
    default: return ELDGrowthReason::DataInvalid;
    }
}

ELDGrowthReason VendorCombatReason(ELDCombatPrepareError Error)
{
    switch (Error)
    {
    case ELDCombatPrepareError::NumericUnderflow: return ELDGrowthReason::NumericUnderflow;
    case ELDCombatPrepareError::NumericDomain: return ELDGrowthReason::NumericDomain;
    case ELDCombatPrepareError::StaleBuild: return ELDGrowthReason::BuildStale;
    case ELDCombatPrepareError::UnknownContender: return ELDGrowthReason::NotContender;
    case ELDCombatPrepareError::CapacityExceeded: return ELDGrowthReason::CapacityExceeded;
    case ELDCombatPrepareError::InvalidInput:
    case ELDCombatPrepareError::Catalog: return ELDGrowthReason::DataInvalid;
    case ELDCombatPrepareError::NotAuthority:
    case ELDCombatPrepareError::StaleMatch: return ELDGrowthReason::MatchClosed;
    case ELDCombatPrepareError::None: break;
    }
    // A failed prepare without a typed error is a broken Combat contract, never a success.
    return ELDGrowthReason::PrepareUnavailable;
}
}

ALDVendorDirector::ALDVendorDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

ALDGameMode* ALDVendorDirector::Mode() const
{
    UWorld* World = GetWorld();
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

bool ALDVendorDirector::BeginMatch(const FLDMatchContext& InMatch, const TArray<FGuid>& Roster,
    TSharedPtr<const FLDDemoContentSnapshot> InContent, FString& Error)
{
    Error.Reset();
    if (!InMatch.IsValid() || !InContent.IsValid())
    {
        Error = TEXT("The shop needs the current match and the approved Demo catalog.");
        return false;
    }
    if (Roster.IsEmpty())
    {
        Error = TEXT("The shop needs at least one locked roster contender.");
        return false;
    }
    TMap<FGuid, FRecord> NewRecords;
    NewRecords.Reserve(Roster.Num());
    for (const FGuid& Id : Roster)
    {
        if (!Id.IsValid() || NewRecords.Contains(Id))
        {
            Error = TEXT("Shop roster contenders must be valid and distinct.");
            return false;
        }
        NewRecords.Add(Id);
    }
    Match = InMatch;
    Content = MoveTemp(InContent);
    Records = MoveTemp(NewRecords);
    return true;
}

FLDShopOpenResult ALDVendorDirector::QueueShopOpen(const FGuid& ContenderId, const FLDShopOpenCommand& Command)
{
    FLDShopOpenResult Result;
    Result.Generation = Command.Generation;
    Result.Life = Command.Life;
    FName ShopId;
    FLDWorldRevision ExpectedWorld;
    const bool bShape = VendorDecodeOpen(Command, ShopId, ExpectedWorld);
    Result.ShopId = ShopId;
    const auto Reject = [&Result](ELDGrowthReason Why)
    {
        Result.OpenStatus = ELDShopOpenStatus::Rejected;
        Result.Reason = LDGrowthWire::Raw(Why);
        return Result;
    };
    if (!bShape)
    {
        return Reject(ELDGrowthReason::InvalidRequest);
    }
    if (!Match.IsValid() || Command.Generation != Match.Generation)
    {
        return Reject(ELDGrowthReason::OldMatch);
    }
    FRecord* Record = Records.Find(ContenderId);
    if (!Record)
    {
        return Reject(ELDGrowthReason::NotContender);
    }
    if (Record->PendingOpen.IsSet())
    {
        if (!VendorSameOpen(Record->PendingOpen->Command, Command))
        {
            return Reject(ELDGrowthReason::RequestInFlight);
        }
        Result.OpenStatus = ELDShopOpenStatus::Pending;
        return Result;
    }
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Economy)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_SHOP_OPEN_ADMISSION_FAULT contender=%s: no authority mode or economy director"),
            *ContenderId.ToString());
        return Reject(ELDGrowthReason::NotAuthority);
    }
    // A closed match never runs another Transactions phase, so a new intent could never be answered.
    if (!GameMode->IsGameplayWriteOpen(Match))
    {
        return Reject(ELDGrowthReason::MatchClosed);
    }
    FPendingOpen Intent;
    Intent.Command = Command;
    Intent.Arrival = GameMode->Economy->NextArrival();
    Record->PendingOpen.Emplace(MoveTemp(Intent));
    Result.OpenStatus = ELDShopOpenStatus::Pending;
    return Result;
}

FLDShopOpenResult ALDVendorDirector::ReadShop(const FGuid& ContenderId) const
{
    FLDShopOpenResult Result;
    Result.Generation = Match.Generation;
    const auto Reject = [&Result](ELDGrowthReason Why)
    {
        Result.OpenStatus = ELDShopOpenStatus::Rejected;
        Result.Reason = LDGrowthWire::Raw(Why);
        return Result;
    };
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record)
    {
        return Reject(ELDGrowthReason::NotContender);
    }
    const ALDGameMode* GameMode = Mode();
    if (!GameMode)
    {
        return Reject(ELDGrowthReason::NotAuthority);
    }
    FLDContenderLifeRead Life;
    FString LifeError;
    if (!GameMode->ReadContenderLife(Match, ContenderId, Life, LifeError))
    {
        return Reject(ELDGrowthReason::StaleContext);
    }
    Result.Life = Life.Life;
    if (Record->PendingOpen.IsSet() && Record->PendingOpen->Command.Life == Life.Life)
    {
        FName PendingShop;
        Result.ShopId = Record->PendingOpen->Command.ShopId.Decode(PendingShop) ? PendingShop : FName();
        Result.OpenStatus = ELDShopOpenStatus::Pending;
        return Result;
    }
    if (Record->Session.IsSet() && Record->Session->Life == Life.Life && Life.Phase == ELDLifePhase::Alive)
    {
        Result.ShopId = Record->Session->ShopId;
        FLDShopView View;
        if (!BuildView(ContenderId, Record->Session.GetValue(), View))
        {
            return Reject(ELDGrowthReason::DataNotReady);
        }
        Result.OpenStatus = ELDShopOpenStatus::Opened;
        Result.bHasView = true;
        Result.View = MoveTemp(View);
        return Result;
    }
    const FLDShopOpenResult& Last = Record->LastOpen;
    if (Last.OpenStatus == ELDShopOpenStatus::Rejected && Last.Generation == Match.Generation && Last.Life == Life.Life)
    {
        Result.ShopId = Last.ShopId;
        return Reject(LDGrowthWire::Reason(Last.Reason));
    }
    return Result;
}

bool ALDVendorDirector::BuildView(const FGuid& ContenderId, const FSession& Session, FLDShopView& Out) const
{
    Out = FLDShopView();
    const ALDGameMode* GameMode = Mode();
    const UWorld* World = GetWorld();
    const ULDMapRegistry* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Content.IsValid() || !GameMode || !GameMode->Economy || !GameMode->Development || !GameMode->Combat
        || !Registry || !Registry->Enabled())
    {
        return false;
    }
    const LDEconomy::FLedger& Book = GameMode->Economy->GetLedger();
    if (!Book.IsInitialized() || !Book.Account(ContenderId))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const FLDWorldRevision& WorldRevision = Registry->Revision();
    Out.ShopId = Session.ShopId;
    Out.SessionId = Session.SessionId;
    Out.Life = Session.Life;
    Out.QuoteRevision = Session.QuoteRevision;
    Out.CatalogDigest = Definitions.Identity.Root;
    Out.WorldMapId = WorldRevision.MapId;
    Out.WorldLayoutRevision = WorldRevision.LayoutRevision;
    Out.WorldGeometryRevision = WorldRevision.GeometryRevision;
    TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
    Book.OwnerItems(ContenderId, Owned);
    Out.Items.Reserve(Definitions.Equipment.Num());
    for (const FLDDemoEquipmentDefinition& Definition : Definitions.Equipment)
    {
        FLDShopItemView& Item = Out.Items.AddDefaulted_GetRef();
        Item.DefinitionId = FName(*Definition.Id);
        Item.BuyPrice = Definition.BuyPrice;
        Item.SellGold = Definition.SellGold;
    }
    const LDEconomy::FAccount* Account = Book.Account(ContenderId);
    Out.UpgradePrice = Definitions.Parameters.UpgradeCost;
    Out.bUpgradeUsed = Account->bUpgradeUsed;
    Out.Sellable.Reserve(Owned.Num());
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : Owned)
    {
        const FLDDemoEquipmentDefinition* Definition = VendorFindEquipment(*Content, Pair.Value.Definition);
        if (!Definition)
        {
            Out = FLDShopView();
            return false;
        }
        FLDShopSellView& Sell = Out.Sellable.AddDefaulted_GetRef();
        Sell.SlotIndex = Pair.Value.Slot;
        Sell.Instance = Pair.Key;
        Sell.ItemRevision = Pair.Value.Revision;
        Sell.DefinitionId = Pair.Value.Definition;
        Sell.SellPrice = Definition->SellGold;
        Sell.bUpgraded = Pair.Value.bUpgraded;
        Sell.bCanUpgrade = !Account->bUpgradeUsed && !Pair.Value.bUpgraded && LDDemoEquipment::Upgradable(*Definition);
    }
    return true;
}

FLDSubmitResult ALDVendorDirector::QueueShopCommand(const FGuid& ContenderId, const FLDShopCommand& Command)
{
    FLDSubmitResult Result;
    Result.Generation = Command.Generation;
    Result.Family = ELDGrowthFamily::Shop;
    Result.RequestId = Command.RequestId;
    const auto Reject = [&Result](ELDGrowthReason Why)
    {
        Result.Status = ELDAdmissionStatus::AdmissionRejected;
        Result.Reason = LDGrowthWire::Raw(Why);
        return Result;
    };
    if (!VendorCommandShape(Command))
    {
        return Reject(ELDGrowthReason::InvalidRequest);
    }
    if (!Match.IsValid() || Command.Generation != Match.Generation)
    {
        return Reject(ELDGrowthReason::OldMatch);
    }
    FRecord* Record = Records.Find(ContenderId);
    if (!Record)
    {
        return Reject(ELDGrowthReason::NotContender);
    }
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Economy)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_SHOP_COMMAND_ADMISSION_FAULT contender=%s: no authority mode or economy director"),
            *ContenderId.ToString());
        return Reject(ELDGrowthReason::NotAuthority);
    }
    // Only a request that opens the next sequence takes a server arrival; a closed match opens none, while
    // replays of an accepted or final request still answer from the stream.
    const bool bOpensSequence = !Record->Requests.GetPending()
        && Command.RequestId == Record->Requests.GetFinalizedThrough() + 1;
    if (bOpensSequence && !GameMode->IsGameplayWriteOpen(Match))
    {
        return Reject(ELDGrowthReason::MatchClosed);
    }
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    Info.OriginalLifePhase = ELDLifePhase::Alive;
    const uint64 Arrival = bOpensSequence ? GameMode->Economy->NextArrival() : 0;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDShopReceipt* Final = nullptr;
    bool bReserved = false;
    Result.Status = Record->Requests.Admit(Command, Command.RequestId, Info, Arrival, Reason, Final, bReserved);
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.bSequenceReserved = bReserved;
    if (Result.Status == ELDAdmissionStatus::Final && Final)
    {
        Result.bHasShopReceipt = true;
        Result.ShopReceipt = *Final;
    }
    return Result;
}

FLDShopReceiptQueryResult ALDVendorDirector::QueryShopReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const
{
    FLDShopReceiptQueryResult Result;
    Result.Query = Query;
    const FRecord* Record = Records.Find(ContenderId);
    if (Query.Family != ELDGrowthFamily::Shop || !Match.IsValid() || Query.Generation != Match.Generation || !Record)
    {
        Result.Lookup = ELDReceiptLookup::InvalidContext;
        return Result;
    }
    const FLDShopReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* PendingInfo = nullptr;
    Result.Lookup = Record->Requests.Lookup(Query.RequestId, Final, PendingInfo);
    if (Result.Lookup == ELDReceiptLookup::Pending && PendingInfo)
    {
        Result.bHasPending = true;
        Result.Pending = *PendingInfo;
    }
    else if (Result.Lookup == ELDReceiptLookup::Final && Final)
    {
        Result.bHasReceipt = true;
        Result.Receipt = *Final;
    }
    return Result;
}

void ALDVendorDirector::CollectPending(TArray<FLDPendingGrowthIntent>& Out) const
{
    for (const auto& Pair : Records)
    {
        if (Pair.Value.PendingOpen.IsSet())
        {
            FLDPendingGrowthIntent& Intent = Out.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::ShopOpen;
            Intent.Contender = Pair.Key;
            Intent.Arrival = Pair.Value.PendingOpen->Arrival;
        }
        if (const auto* PendingCommand = Pair.Value.Requests.GetPending())
        {
            FLDPendingGrowthIntent& Intent = Out.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::ShopCommand;
            Intent.Contender = Pair.Key;
            Intent.Arrival = PendingCommand->Arrival;
        }
    }
}

FLDShopOpenResult ALDVendorDirector::OpenPendingShop(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit)
{
    FLDShopOpenResult Result;
    Result.Generation = Match.Generation;
    FRecord* Record = Records.Find(ContenderId);
    ALDGameMode* GameMode = Mode();
    // An unusable permit or a broken dispatch contract consumes nothing; the boundary must fault.
    if (!Record || !Record->PendingOpen.IsSet() || !GameMode || !GameMode->Economy || !Content.IsValid()
        || GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        if (Record && Record->PendingOpen.IsSet())
        {
            Result.Generation = Record->PendingOpen->Command.Generation;
            Result.Life = Record->PendingOpen->Command.Life;
        }
        Result.OpenStatus = ELDShopOpenStatus::Rejected;
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::AuthorityFault);
        UE_LOG(LogLDGrowth, Error, TEXT("LD_SHOP_OPEN_FAULT contender=%s: no pending open, growth domain or current Transactions permit"),
            *ContenderId.ToString());
        return Result;
    }
    const FLDShopOpenCommand Command = Record->PendingOpen->Command;
    Record->PendingOpen.Reset();
    Result.Generation = Command.Generation;
    Result.Life = Command.Life;
    FName ShopId;
    FLDWorldRevision ExpectedWorld;
    const bool bShape = VendorDecodeOpen(Command, ShopId, ExpectedWorld);
    Result.ShopId = ShopId;
    // Every terminal outcome becomes the last open result and is delivered with the next publication.
    const auto Complete = [&](ELDGrowthReason Why)
    {
        Result.OpenStatus = Why == ELDGrowthReason::None ? ELDShopOpenStatus::Opened : ELDShopOpenStatus::Rejected;
        Result.Reason = LDGrowthWire::Raw(Why);
        if (Why != ELDGrowthReason::None && Record->Session.IsSet() && Record->Session->Life == Command.Life)
        {
            // A failed open leaves no executable session of that life behind.
            Record->Session.Reset();
        }
        FLDShopOpenResult Last = Result;
        Last.bHasView = false;
        Last.View = FLDShopView();
        Record->LastOpen = MoveTemp(Last);
        Record->UnpublishedOpen.Emplace(Result);
        GameMode->Economy->MarkDirty(ContenderId);
        return Result;
    };
    if (!bShape)
    {
        return Complete(ELDGrowthReason::InvalidRequest);
    }
    if (Command.Generation != Match.Generation)
    {
        return Complete(ELDGrowthReason::OldMatch);
    }
    FLDContenderLifeRead Life;
    FString LifeError;
    if (!GameMode->ReadContenderLife(Match, ContenderId, Life, LifeError))
    {
        return Complete(ELDGrowthReason::StaleContext);
    }
    if (Life.Phase == ELDLifePhase::Left)
    {
        return Complete(ELDGrowthReason::LeftMatch);
    }
    if (Life.Life != Command.Life)
    {
        return Complete(ELDGrowthReason::OldLife);
    }
    if (Life.Phase != ELDLifePhase::Alive)
    {
        return Complete(ELDGrowthReason::Dead);
    }
    if (!GameMode->IsGameplayWriteOpen(Match))
    {
        return Complete(ELDGrowthReason::MatchClosed);
    }
    if (!Content->Get().World.ShopIds.Contains(ShopId.ToString()))
    {
        return Complete(ELDGrowthReason::UnknownShop);
    }
    const UWorld* World = GetWorld();
    const ULDMapRegistry* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry || !Registry->Enabled())
    {
        return Complete(ELDGrowthReason::DataNotReady);
    }
    if (ExpectedWorld != Registry->Revision())
    {
        return Complete(ELDGrowthReason::StaleWorld);
    }
    const ELDGrowthReason Eligibility = VendorEligibilityReason(
        Registry->QueryShopEligibility(Permit, Life.Body, ShopId, ExpectedWorld));
    if (Eligibility != ELDGrowthReason::None)
    {
        return Complete(Eligibility);
    }
    FSession Session;
    if (Record->Session.IsSet() && Record->Session->Life == Life.Life)
    {
        Session = Record->Session.GetValue();
    }
    else
    {
        Session.SessionId = FGuid::NewGuid();
        Session.Life = Life.Life;
        Session.QuoteRevision = 1;
    }
    Session.ShopId = ShopId;
    FLDShopView View;
    if (!BuildView(ContenderId, Session, View))
    {
        return Complete(ELDGrowthReason::DataNotReady);
    }
    Record->Session.Emplace(Session);
    Result.bHasView = true;
    Result.View = MoveTemp(View);
    return Complete(ELDGrowthReason::None);
}

FLDShopReceipt ALDVendorDirector::CommitPendingShopCommand(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit)
{
    FRecord* Record = Records.Find(ContenderId);
    const auto* Pending = Record ? Record->Requests.GetPending() : nullptr;
    FLDShopCommand Command;
    FLDPendingReceiptInfo Info;
    if (Pending)
    {
        Command = Pending->Command;
        Info = Pending->Info;
    }
    ALDGameMode* GameMode = Mode();
    // Neither finalized nor applied: the Transactions dispatcher faults the authority boundary.
    const auto Fault = [&](const TCHAR* Diagnostic)
    {
        FLDShopReceipt Receipt;
        Receipt.Kind = Command.Kind;
        FLDGrowthReceiptHeader& Header = Receipt.Header;
        Header.Generation = Command.Generation;
        Header.Family = ELDGrowthFamily::Shop;
        Header.RequestId = Command.RequestId;
        Header.OriginalLife = Info.OriginalLife;
        Header.OriginalLifePhase = Info.OriginalLifePhase;
        Header.Outcome = ELDReceiptOutcome::Rejected;
        Header.Reason = LDGrowthWire::Raw(ELDGrowthReason::AuthorityFault);
        UE_LOG(LogLDGrowth, Error, TEXT("LD_SHOP_COMMIT_FAULT contender=%s request=%llu: %s"), *ContenderId.ToString(),
            Command.RequestId, Diagnostic);
        return Receipt;
    };
    if (!Pending || !GameMode || !GameMode->Economy || !GameMode->Development || !GameMode->Combat || !Content.IsValid())
    {
        return Fault(TEXT("no pending shop command or growth domain"));
    }
    if (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        return Fault(TEXT("the Transactions write permit is not current"));
    }
    const auto Reject = [&](ELDGrowthReason Why) { return Finish(ContenderId, *Record, Why, Permit); };
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    if (Command.Generation != Match.Generation)
    {
        return Reject(ELDGrowthReason::OldMatch);
    }
    if (Command.CatalogDigest != Definitions.Identity.Root)
    {
        return Reject(ELDGrowthReason::CatalogMismatch);
    }
    FLDContenderLifeRead Life;
    FString LifeError;
    if (!GameMode->ReadContenderLife(Match, ContenderId, Life, LifeError))
    {
        return Reject(ELDGrowthReason::StaleContext);
    }
    if (Life.Phase == ELDLifePhase::Left)
    {
        return Reject(ELDGrowthReason::LeftMatch);
    }
    if (Life.Life != Command.Life)
    {
        return Reject(ELDGrowthReason::OldLife);
    }
    if (Life.Phase != ELDLifePhase::Alive)
    {
        return Reject(ELDGrowthReason::Dead);
    }
    if (!GameMode->IsGameplayWriteOpen(Match))
    {
        return Reject(ELDGrowthReason::MatchClosed);
    }
    if (!Record->Session.IsSet() || Record->Session->SessionId != Command.SessionId || Record->Session->Life != Life.Life)
    {
        return Reject(ELDGrowthReason::NoShopSession);
    }
    if (Record->Session->QuoteRevision != Command.QuoteRevision)
    {
        return Reject(ELDGrowthReason::QuoteStale);
    }
    const UWorld* World = GetWorld();
    const ULDMapRegistry* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry || !Registry->Enabled())
    {
        return Reject(ELDGrowthReason::DataNotReady);
    }
    FLDWorldRevision ExpectedWorld;
    ExpectedWorld.LayoutRevision = Command.WorldLayoutRevision;
    ExpectedWorld.GeometryRevision = Command.WorldGeometryRevision;
    if (!Command.WorldMapId.Decode(ExpectedWorld.MapId) || ExpectedWorld != Registry->Revision())
    {
        return Reject(ELDGrowthReason::StaleWorld);
    }
    // Death, reach, grounding and the match gate are re-sampled for this commit; the open never authorizes it.
    const ELDGrowthReason Eligibility = VendorEligibilityReason(
        Registry->QueryShopEligibility(Permit, Life.Body, Record->Session->ShopId, ExpectedWorld));
    if (Eligibility != ELDGrowthReason::None)
    {
        return Reject(Eligibility);
    }
    LDEconomy::FLedger& Ledger = GameMode->Economy->MutableLedger();
    const LDEconomy::FAccount* Account = Ledger.Account(ContenderId);
    if (!Account)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_SHOP_ACCOUNT_MISSING contender=%s"), *ContenderId.ToString());
        return Reject(ELDGrowthReason::DataInvalid);
    }
    if (Account->Revision != Command.ExpectedAccountRevision)
    {
        return Reject(ELDGrowthReason::AccountStale);
    }
    if (Ledger.InventoryRevision(ContenderId) != Command.ExpectedInventoryRevision)
    {
        return Reject(ELDGrowthReason::InventoryStale);
    }
    const FLDGrowthBuild* Build = GameMode->Development->FindBuild(ContenderId);
    if (!Build)
    {
        return Reject(ELDGrowthReason::NotContender);
    }
    if (Build->Revision != Command.ExpectedBuildRevision)
    {
        return Reject(ELDGrowthReason::BuildStale);
    }

    TArray<uint8> Canonical;
    VendorCanonicalCommand(Match.LedgerMatch, ContenderId, Command, Canonical);
    FLDShopReceipt Receipt;
    const ELDGrowthReason TradeReason = CommitTrade(ContenderId, Command, Canonical, Permit, Receipt);
    if (TradeReason == ELDGrowthReason::AuthorityFault)
    {
        return Fault(TEXT("the Transactions write permit expired before the composite install"));
    }
    if (TradeReason != ELDGrowthReason::None)
    {
        return Reject(TradeReason);
    }
    return Finish(ContenderId, *Record, ELDGrowthReason::None, Permit, MoveTemp(Receipt));
}

ELDGrowthReason ALDVendorDirector::CommitTrade(const FGuid& ContenderId, const FLDShopCommand& Command,
    const TArray<uint8>& Canonical, const FLDGameplayWritePermit& Permit, FLDShopReceipt& OutReceipt)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Economy || !GameMode->Development || !GameMode->Combat || !Content.IsValid())
    {
        return ELDGrowthReason::AuthorityFault;
    }
    LDEconomy::FLedger& Ledger = GameMode->Economy->MutableLedger();
    const LDEconomy::FAccount* Account = Ledger.Account(ContenderId);
    if (!Account)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const bool bBuy = Command.Kind == ELDShopCommandKind::Buy;
    const bool bUpgrade = Command.Kind == ELDShopCommandKind::Upgrade;
    LDEconomy::FTransaction Transaction;
    Transaction.BusinessKey = ALDEconomyDirector::BusinessKey(Canonical);
    ALDEconomyDirector::Terms(Canonical, Transaction.Terms);
    Transaction.CommitBatch = Permit.GetBoundary().BatchSerial;
    LDEconomy::FGoldChange Gold;
    Gold.Account = ContenderId;
    Gold.ExpectedRevision = Command.ExpectedAccountRevision;
    FName DefinitionId;
    uint8 SlotIndex = 0;
    uint64 SoldIssueSerial = 0;
    if (bBuy)
    {
        FName WireDefinition;
        const FLDDemoEquipmentDefinition* Definition = Command.DefinitionId.Decode(WireDefinition)
            ? VendorFindEquipment(*Content, WireDefinition) : nullptr;
        if (!Definition)
        {
            return ELDGrowthReason::UnknownItem;
        }
        if (Command.BuyPrice != Definition->BuyPrice)
        {
            return ELDGrowthReason::PriceChanged;
        }
        TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
        Ledger.OwnerItems(ContenderId, Owned);
        bool bTaken[LDEconomy::EquipmentSlots] = {};
        for (const TPair<FGuid, LDEconomy::FItem>& Pair : Owned)
        {
            if (Pair.Value.Slot < LDEconomy::EquipmentSlots)
            {
                bTaken[Pair.Value.Slot] = true;
            }
        }
        int32 FreeSlot = INDEX_NONE;
        for (int32 Slot = 0; Slot < LDEconomy::EquipmentSlots && FreeSlot == INDEX_NONE; ++Slot)
        {
            if (!bTaken[Slot])
            {
                FreeSlot = Slot;
            }
        }
        // Both positions held: never auto-sell or replace; the owner sells first in a separate request.
        // Capacity is reported before the balance because selling first is the only way forward.
        if (Owned.Num() >= LDEconomy::EquipmentSlots || FreeSlot == INDEX_NONE)
        {
            return ELDGrowthReason::EquipmentFull;
        }
        if (Account->Gold < Command.BuyPrice)
        {
            return ELDGrowthReason::InsufficientGold;
        }
        DefinitionId = FName(*Definition->Id);
        SlotIndex = uint8(FreeSlot);
        Gold.Delta = -Command.BuyPrice;
        Transaction.Cause = LDEconomy::ECause::Buy;
        Transaction.Burn = Command.BuyPrice;
        LDEconomy::FCreateItem Create;
        Create.Owner = ContenderId;
        Create.ExpectedInventoryRevision = Command.ExpectedInventoryRevision;
        Create.Definition = DefinitionId;
        Create.Slot = SlotIndex;
        Transaction.Create.Emplace(Create);
    }
    else if (bUpgrade)
    {
        // 112 §7: one upgrade per contender and match, of an owned not yet upgraded instance with approved upgraded terms.
        const LDEconomy::FItem* Item = Ledger.Item(Command.Instance);
        if (!Item || Item->Owner != ContenderId)
        {
            return ELDGrowthReason::ItemNotOwned;
        }
        if (Item->Revision != Command.ExpectedItemRevision)
        {
            return ELDGrowthReason::ItemStale;
        }
        const FLDDemoEquipmentDefinition* Definition = VendorFindEquipment(*Content, Item->Definition);
        if (!Definition)
        {
            return ELDGrowthReason::DataInvalid;
        }
        if (Account->bUpgradeUsed)
        {
            return ELDGrowthReason::UpgradeUsed;
        }
        if (Item->bUpgraded)
        {
            return ELDGrowthReason::AlreadyUpgraded;
        }
        if (!LDDemoEquipment::Upgradable(*Definition))
        {
            return ELDGrowthReason::UpgradeUnavailable;
        }
        const int64 Price = Content->Get().Parameters.UpgradeCost;
        if (Command.UpgradePrice != Price)
        {
            return ELDGrowthReason::PriceChanged;
        }
        if (Account->Gold < Price)
        {
            return ELDGrowthReason::InsufficientGold;
        }
        DefinitionId = Item->Definition;
        SlotIndex = Item->Slot;
        SoldIssueSerial = Item->IssueSerial;
        Gold.Delta = -Price;
        Transaction.Cause = LDEconomy::ECause::Upgrade;
        Transaction.Burn = Price;
        LDEconomy::FUpgradeItem Upgrade;
        Upgrade.Instance = Command.Instance;
        Upgrade.ExpectedItemRevision = Command.ExpectedItemRevision;
        Upgrade.Owner = ContenderId;
        Upgrade.ExpectedInventoryRevision = Command.ExpectedInventoryRevision;
        Transaction.Upgrade.Emplace(Upgrade);
    }
    else
    {
        const LDEconomy::FItem* Item = Ledger.Item(Command.Instance);
        if (!Item || Item->Owner != ContenderId)
        {
            return ELDGrowthReason::ItemNotOwned;
        }
        if (Item->Revision != Command.ExpectedItemRevision)
        {
            return ELDGrowthReason::ItemStale;
        }
        const FLDDemoEquipmentDefinition* Definition = VendorFindEquipment(*Content, Item->Definition);
        if (!Definition)
        {
            return ELDGrowthReason::DataInvalid;
        }
        if (Command.SellPrice != Definition->SellGold)
        {
            return ELDGrowthReason::PriceChanged;
        }
        DefinitionId = Item->Definition;
        SlotIndex = Item->Slot;
        SoldIssueSerial = Item->IssueSerial;
        Gold.Delta = Command.SellPrice;
        Transaction.Cause = LDEconomy::ECause::Sell;
        Transaction.Mint = Command.SellPrice;
        LDEconomy::FConsumeItem Consume;
        Consume.Instance = Command.Instance;
        Consume.ExpectedItemRevision = Command.ExpectedItemRevision;
        Consume.Owner = ContenderId;
        Consume.ExpectedInventoryRevision = Command.ExpectedInventoryRevision;
        Transaction.Consume.Emplace(Consume);
    }
    Transaction.Gold.Emplace(Gold);

    // Pure prepare of every part; nothing below changes gold, items, build or sources until the final install.
    Ledger.Pin();
    ON_SCOPE_EXIT
    {
        Ledger.Unpin();
    };
    LDEconomy::FPrepareResult PreparedLedger = Ledger.Prepare(Transaction);
    if (!PreparedLedger.IsSuccess())
    {
        const ELDGrowthReason LedgerReason = VendorLedgerReason(PreparedLedger.Error);
        if (LedgerReason == ELDGrowthReason::DataInvalid)
        {
            UE_LOG(LogLDGrowth, Warning, TEXT("LD_SHOP_LEDGER_REJECTED contender=%s request=%llu %s: %s"), *ContenderId.ToString(),
                Command.RequestId, LDEconomy::FLedger::ErrorName(PreparedLedger.Error), *PreparedLedger.Message);
        }
        return LedgerReason;
    }
    const LDEconomy::FPreparedCommit& LedgerPlan = PreparedLedger.Value.GetValue();
    const uint64* AfterInventory = LedgerPlan.After().InventoryRevisions.Find(ContenderId);
    if (!AfterInventory)
    {
        return ELDGrowthReason::DataInvalid;
    }
    const uint64 ResultInventoryRevision = *AfterInventory;

    FLDPrepareGrowthResult PreparedGrowth = GameMode->Development->PrepareEquipmentChange(ContenderId);
    if (!PreparedGrowth.IsSuccess())
    {
        return PreparedGrowth.Reason != ELDGrowthReason::None ? PreparedGrowth.Reason : ELDGrowthReason::PrepareUnavailable;
    }
    const uint64 ResultBuildRevision = PreparedGrowth.Value->Build().Revision;

    TArray<FLDEquippedItemValue> Equipped;
    ALDInventoryDirector::EquippedItems(LedgerPlan.After(), ContenderId, Equipped);
    FLDExpectedBuildVersions Installed;
    if (!GameMode->Development->ReadCombatInstalled(ContenderId, Installed))
    {
        return ELDGrowthReason::NotContender;
    }
    FLDCombatPrepareResult PreparedCombat = GameMode->Combat->PrepareBuildChange(GameMode->Development->MakeCaller(ContenderId),
        GameMode->Development->MakeProjectionInput(PreparedGrowth.Value->Build(), Equipped, ResultInventoryRevision), Installed);
    if (!PreparedCombat.IsSuccess())
    {
        const ELDGrowthReason CombatReason = VendorCombatReason(PreparedCombat.Error);
        if (CombatReason == ELDGrowthReason::DataInvalid || CombatReason == ELDGrowthReason::PrepareUnavailable)
        {
            UE_LOG(LogLDGrowth, Warning, TEXT("LD_SHOP_PROJECTION_REJECTED contender=%s request=%llu %s: %s"), *ContenderId.ToString(),
                Command.RequestId, LDGrowthWire::ReasonName(CombatReason), *PreparedCombat.Message);
        }
        return CombatReason;
    }

    const LDEconomy::FJournalEntry& Entry = LedgerPlan.Entry();
    FLDShopReceipt& Receipt = OutReceipt;
    Receipt.Instance = bBuy ? Entry.CreatedInstance : Command.Instance;
    Receipt.IssueSerial = bBuy ? Entry.CreatedIssueSerial : SoldIssueSerial;
    Receipt.DefinitionId = DefinitionId;
    Receipt.SlotIndex = SlotIndex;
    Receipt.ActualGoldDelta = Gold.Delta;
    Receipt.bHasLedgerSerial = true;
    Receipt.LedgerSerial = Entry.Serial;
    FLDResultRevisions& Revisions = Receipt.Header.Revisions;
    Revisions.bHasAccount = true;
    Revisions.AccountRevision = Entry.ResultAccountRevision;
    Revisions.bHasInventory = true;
    Revisions.InventoryRevision = ResultInventoryRevision;
    Revisions.bHasBuild = true;
    Revisions.BuildRevision = ResultBuildRevision;

    if (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        return ELDGrowthReason::AuthorityFault;
    }
    // Every part is prepared and the permit re-checked; the composite install below cannot fail.
    FLDExpectedBuildVersions Install;
    Install.BuildRevision = ResultBuildRevision;
    Install.InventoryRevision = ResultInventoryRevision;
    Ledger.ApplyPrepared(MoveTemp(PreparedLedger.Value.GetValue()));
    PreparedGrowth.Value->SetCombatInstall(Install);
    GameMode->Development->ApplyPreparedGrowth(MoveTemp(PreparedGrowth.Value.GetValue()), Permit);
    GameMode->Combat->ApplyBuildChange(MoveTemp(PreparedCombat.Value.GetValue()), Permit);
    // Host playtest record of the committed trade (failures keep their own lines and receipts).
    const LDEconomy::FAccount* After = Ledger.Account(ContenderId);
    UE_LOG(LogLDGrowth, Log, TEXT("LD_SHOP_TRADE t=%lld contender=%s kind=%s item=%s slot=%u gold_delta=%lld gold_after=%lld request=%llu"),
        Permit.GetBoundary().AtMs, *LDLog::Id(ContenderId), *LDLog::EnumName(Command.Kind), *DefinitionId.ToString(),
        uint32(SlotIndex) + 1, Gold.Delta, After ? After->Gold : int64(-1), Command.RequestId);
    return ELDGrowthReason::None;
}

FLDShopReceipt ALDVendorDirector::Finish(const FGuid& ContenderId, FRecord& Record, ELDGrowthReason Reason,
    const FLDGameplayWritePermit& Permit, FLDShopReceipt Receipt)
{
    const auto* Pending = Record.Requests.GetPending();
    check(Pending != nullptr);
    ALDGameMode* GameMode = Mode();
    if (Reason != ELDGrowthReason::None)
    {
        // Rejected: no committed payload; the current guards are sync hints only, never committed versions.
        Receipt = FLDShopReceipt();
        FLDResultRevisions& Hints = Receipt.Header.Revisions;
        if (GameMode && GameMode->Economy)
        {
            const LDEconomy::FLedger& Book = GameMode->Economy->GetLedger();
            if (const LDEconomy::FAccount* Account = Book.Account(ContenderId))
            {
                Hints.bHasAccount = true;
                Hints.AccountRevision = Account->Revision;
            }
            if (const uint64 CurrentInventory = Book.InventoryRevision(ContenderId))
            {
                Hints.bHasInventory = true;
                Hints.InventoryRevision = CurrentInventory;
            }
        }
        if (const FLDGrowthBuild* Build = (GameMode && GameMode->Development) ? GameMode->Development->FindBuild(ContenderId) : nullptr)
        {
            Hints.bHasBuild = true;
            Hints.BuildRevision = Build->Revision;
        }
    }
    Receipt.Kind = Pending->Command.Kind;
    FLDGrowthReceiptHeader& Header = Receipt.Header;
    Header.Generation = Pending->Command.Generation;
    Header.Family = ELDGrowthFamily::Shop;
    Header.RequestId = Pending->Command.RequestId;
    Header.OriginalLife = Pending->Info.OriginalLife;
    Header.OriginalLifePhase = Pending->Info.OriginalLifePhase;
    Header.Outcome = Reason == ELDGrowthReason::None ? ELDReceiptOutcome::Applied : ELDReceiptOutcome::Rejected;
    Header.Reason = LDGrowthWire::Raw(Reason);
    if (Reason != ELDGrowthReason::None)
    {
        UE_LOG(LogLDGrowth, Log, TEXT("LD_SHOP_REJECTED t=%lld contender=%s kind=%s request=%llu reason=%s"), Permit.GetBoundary().AtMs,
            *LDLog::Id(ContenderId), *LDLog::EnumName(Pending->Command.Kind), Pending->Command.RequestId, LDGrowthWire::ReasonName(Reason));
    }
    Header.bHasCommitBatch = true;
    Header.CommitBatch = Permit.GetBoundary().BatchSerial;
    Record.Requests.Finalize(Receipt);
    Record.Unpublished.Add(Receipt);
    if (GameMode && GameMode->Economy)
    {
        GameMode->Economy->MarkDirty(ContenderId);
    }
    return Receipt;
}

void ALDVendorDirector::TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDShopReceipt>& Out)
{
    Out.Reset();
    if (FRecord* Record = Records.Find(ContenderId))
    {
        Out = MoveTemp(Record->Unpublished);
    }
}

bool ALDVendorDirector::TakeOpenResult(const FGuid& ContenderId, FLDShopOpenResult& Out)
{
    FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Record->UnpublishedOpen.IsSet())
    {
        return false;
    }
    Out = MoveTemp(Record->UnpublishedOpen.GetValue());
    Record->UnpublishedOpen.Reset();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
ELDGrowthReason ALDVendorDirector::DevTrade(const FGuid& ContenderId, bool bBuy, FName ItemId, uint8 Slot, uint64 RequestId,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Economy || !Content.IsValid())
    {
        return ELDGrowthReason::AuthorityFault;
    }
    const LDEconomy::FLedger& Ledger = GameMode->Economy->GetLedger();
    const LDEconomy::FAccount* Account = Ledger.Account(ContenderId);
    const FLDGrowthBuild* Build = GameMode->Development ? GameMode->Development->FindBuild(ContenderId) : nullptr;
    if (!Account || !Build)
    {
        Detail = TEXT("The contender has no account or growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    // The command a shop session would have sent for this trade, at the current guards and the catalog price.
    FLDShopCommand Command;
    Command.Generation = Match.Generation;
    Command.RequestId = RequestId;
    Command.Kind = bBuy ? ELDShopCommandKind::Buy : ELDShopCommandKind::Sell;
    Command.ExpectedAccountRevision = Account->Revision;
    Command.ExpectedInventoryRevision = Ledger.InventoryRevision(ContenderId);
    Command.ExpectedBuildRevision = Build->Revision;
    if (bBuy)
    {
        const FLDDemoEquipmentDefinition* Definition = VendorFindEquipment(*Content, ItemId);
        if (!Definition || !FLDDefinitionWire::Encode(ItemId, Command.DefinitionId))
        {
            Detail = FString::Printf(TEXT("'%s' is not an item of the approved catalog."), *ItemId.ToString());
            return ELDGrowthReason::UnknownItem;
        }
        Command.BuyPrice = Definition->BuyPrice;
    }
    else
    {
        TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
        Ledger.OwnerItems(ContenderId, Owned);
        const TPair<FGuid, LDEconomy::FItem>* Held = Owned.FindByPredicate(
            [Slot](const TPair<FGuid, LDEconomy::FItem>& Pair) { return Pair.Value.Slot == uint8(Slot - 1); });
        const FLDDemoEquipmentDefinition* Definition = Held ? VendorFindEquipment(*Content, Held->Value.Definition) : nullptr;
        if (!Held)
        {
            Detail = FString::Printf(TEXT("Equipment slot %u is empty."), uint32(Slot));
            return ELDGrowthReason::ItemNotOwned;
        }
        if (!Definition)
        {
            return ELDGrowthReason::DataInvalid;
        }
        Command.Instance = Held->Key;
        Command.ExpectedItemRevision = Held->Value.Revision;
        Command.SellPrice = Definition->SellGold;
    }
    // Its own business identity: never the key of a shop request of any session.
    TArray<uint8> Canonical;
    {
        FMemoryWriter Writer(Canonical);
        FString Tag = TEXT("dev-test-trade");
        FGuid LedgerMatch = Match.LedgerMatch;
        FGuid Contender = ContenderId;
        uint64 Request = RequestId;
        uint8 Kind = uint8(Command.Kind);
        FString Item = ItemId.ToString();
        FGuid Instance = Command.Instance;
        Writer << Tag << LedgerMatch << Contender << Request << Kind << Item << Instance;
    }
    FLDShopReceipt Receipt;
    const ELDGrowthReason Reason = CommitTrade(ContenderId, Command, Canonical, Permit, Receipt);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%s %s in slot %u, gold %+lld"), bBuy ? TEXT("bought") : TEXT("sold"),
            *Receipt.DefinitionId.ToString(), uint32(Receipt.SlotIndex) + 1, Receipt.ActualGoldDelta);
        GameMode->Economy->MarkDirty(ContenderId);
    }
    return Reason;
}
#endif
