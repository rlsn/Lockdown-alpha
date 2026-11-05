#include "Framework/LDPlayerController.h"

#include "Development/LDDevelopmentDirector.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDVendorDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"

namespace
{
FLDSubmitResult GrowthServerUnavailable(uint32 Generation, ELDGrowthFamily Family, uint64 RequestId)
{
    FLDSubmitResult Result;
    Result.Generation = Generation;
    Result.Family = Family;
    Result.RequestId = RequestId;
    Result.Status = ELDAdmissionStatus::AdmissionRejected;
    Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::DataNotReady);
    return Result;
}
}

uint64 ALDPlayerController::BeginGrowthRequest(FGrowthRequestCursor& Cursor, ELDGrowthReason& OutLocalReason)
{
    OutLocalReason = ELDGrowthReason::None;
    if (OwnerState.Match == 0 || !OwnerState.Growth.Progression.bValid)
    {
        OutLocalReason = ELDGrowthReason::DataNotReady;
        return 0;
    }
    if (Cursor.Generation != OwnerState.Match)
    {
        Cursor = FGrowthRequestCursor();
        Cursor.Generation = OwnerState.Match;
    }
    if (Cursor.InFlight != 0)
    {
        OutLocalReason = ELDGrowthReason::RequestInFlight;
        return 0;
    }
    if (Cursor.Next == MAX_uint64)
    {
        OutLocalReason = ELDGrowthReason::CapacityExceeded;
        return 0;
    }
    Cursor.InFlight = Cursor.Next;
    return Cursor.InFlight;
}

void ALDPlayerController::SettleGrowthRequest(FGrowthRequestCursor& Cursor, uint32 Generation, uint64 RequestId,
    bool bReserved, bool bFinal)
{
    if (Generation != Cursor.Generation || RequestId == 0 || RequestId != Cursor.InFlight)
    {
        return;
    }
    if (bFinal)
    {
        Cursor.InFlight = 0;
        Cursor.Next = RequestId + 1;
    }
    else if (!bReserved)
    {
        // Not admitted: the server did not consume this sequence, so it stays the next one.
        Cursor.InFlight = 0;
    }
}

uint64 ALDPlayerController::PeekGrowthRequestId(ELDGrowthFamily Family) const
{
    const FGrowthRequestCursor& Cursor = Family == ELDGrowthFamily::Shop ? ShopCursor : ChoiceCursor;
    if (!LDGrowthWire::IsKnownFamily(Family) || OwnerState.Match == 0)
    {
        return 0;
    }
    if (Cursor.Generation != OwnerState.Match)
    {
        return 1;
    }
    return Cursor.InFlight == 0 && Cursor.Next != MAX_uint64 ? Cursor.Next : 0;
}

uint64 ALDPlayerController::SubmitChoice(const FLDChoiceCommand& Command, ELDGrowthReason& OutLocalReason)
{
    const uint64 RequestId = BeginGrowthRequest(ChoiceCursor, OutLocalReason);
    if (RequestId == 0)
    {
        return 0;
    }
    FLDChoiceCommand Sent = Command;
    Sent.Generation = OwnerState.Match;
    Sent.Life = OwnerState.Life;
    Sent.ExpectedLifePhase = OwnerState.LifePhase;
    Sent.RequestId = RequestId;
    ServerGrowthChoice(Sent);
    return RequestId;
}

uint64 ALDPlayerController::SubmitShopCommand(const FLDShopCommand& Command, ELDGrowthReason& OutLocalReason)
{
    const uint64 RequestId = BeginGrowthRequest(ShopCursor, OutLocalReason);
    if (RequestId == 0)
    {
        return 0;
    }
    FLDShopCommand Sent = Command;
    Sent.Generation = OwnerState.Match;
    Sent.Life = OwnerState.Life;
    Sent.RequestId = RequestId;
    ServerShopCommand(Sent);
    return RequestId;
}

bool ALDPlayerController::SubmitShopOpen(const FLDShopOpenCommand& Command, ELDGrowthReason& OutLocalReason)
{
    OutLocalReason = ELDGrowthReason::None;
    if (OwnerState.Match == 0 || OwnerState.Life == 0)
    {
        OutLocalReason = ELDGrowthReason::DataNotReady;
        return false;
    }
    FLDShopOpenCommand Sent = Command;
    Sent.Generation = OwnerState.Match;
    Sent.Life = OwnerState.Life;
    ServerShopOpen(Sent);
    return true;
}

void ALDPlayerController::BindGrowthClient()
{
    if (Navigation)
    {
        Navigation->OnShopArrived.AddUObject(this, &ALDPlayerController::HandleShopArrived);
    }
}

void ALDPlayerController::HandleShopArrived(FName ShopId, uint32 Life)
{
    if (!IsLocalController() || !GetLocalPlayer())
    {
        return;
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    if (RequestShopSession(ShopId, Life, Reason))
    {
        return;
    }
    // Nothing was sent: report the local refusal on the same channel as the server's open results.
    FLDShopOpenResult Refused;
    Refused.Generation = OwnerState.Match;
    Refused.Life = Life;
    Refused.ShopId = ShopId;
    Refused.OpenStatus = ELDShopOpenStatus::Rejected;
    Refused.Reason = LDGrowthWire::Raw(Reason);
    OnShopOpenResult.Broadcast(Refused);
}

bool ALDPlayerController::RequestShopSession(FName ShopId, uint32 Life, ELDGrowthReason& Reason)
{
    const ULDMapRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    FLDShopOpenCommand Command;
    Reason = ELDGrowthReason::None;
    if (OwnerState.Match == 0 || !Registry || !Registry->Revision().IsValid())
    {
        Reason = ELDGrowthReason::DataNotReady;
    }
    else if (Life == 0 || Life != OwnerState.Life)
    {
        Reason = ELDGrowthReason::OldLife;
    }
    else if (OwnerState.LifePhase != ELDLifePhase::Alive)
    {
        Reason = ELDGrowthReason::Dead;
    }
    else if (!FLDDefinitionWire::Encode(ShopId, Command.ShopId))
    {
        Reason = ELDGrowthReason::UnknownShop;
    }
    else if (!FLDDefinitionWire::Encode(Registry->Revision().MapId, Command.WorldMapId))
    {
        Reason = ELDGrowthReason::DataInvalid;
    }
    else
    {
        Command.WorldLayoutRevision = Registry->Revision().LayoutRevision;
        Command.WorldGeometryRevision = Registry->Revision().GeometryRevision;
        return SubmitShopOpen(Command, Reason);
    }
    return false;
}

void ALDPlayerController::QueryGrowthReceipt(const FLDReceiptQuery& Query)
{
    ServerQueryGrowthReceipt(Query);
}

const FLDProgressionView* ALDPlayerController::ReadOwnProgression() const
{
    const FLDProgressionView& View = OwnerState.Growth.Progression;
    return View.bValid && View.Generation == OwnerState.Match ? &View : nullptr;
}

const FLDEquipmentView* ALDPlayerController::ReadOwnEquipment() const
{
    const FLDEquipmentView& View = OwnerState.Growth.Equipment;
    return View.bValid && View.Generation == OwnerState.Match ? &View : nullptr;
}

const FLDShopView* ALDPlayerController::ReadOwnShop() const
{
    const FLDShopOpenResult& Shop = OwnerState.Growth.Shop;
    return Shop.OpenStatus == ELDShopOpenStatus::Opened && Shop.bHasView && Shop.Generation == OwnerState.Match
        && Shop.Life == OwnerState.Life ? &Shop.View : nullptr;
}

void ALDPlayerController::ServerGrowthChoice_Implementation(const FLDChoiceCommand& Command)
{
    ALDGameMode* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
    if (!Mode || !Mode->Development || !Contender.IsValid())
    {
        ClientGrowthSubmit(GrowthServerUnavailable(Command.Generation, ELDGrowthFamily::Choice, Command.RequestId));
        return;
    }
    ClientGrowthSubmit(Mode->Development->QueueChoice(Contender, Command));
}

void ALDPlayerController::ServerShopCommand_Implementation(const FLDShopCommand& Command)
{
    ALDGameMode* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
    if (!Mode || !Mode->Vendor || !Contender.IsValid())
    {
        ClientGrowthSubmit(GrowthServerUnavailable(Command.Generation, ELDGrowthFamily::Shop, Command.RequestId));
        return;
    }
    ClientGrowthSubmit(Mode->Vendor->QueueShopCommand(Contender, Command));
}

void ALDPlayerController::ServerShopOpen_Implementation(const FLDShopOpenCommand& Command)
{
    ALDGameMode* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
    if (!Mode || !Mode->Vendor || !Contender.IsValid())
    {
        FLDShopOpenResult Result;
        Result.Generation = Command.Generation;
        Result.Life = Command.Life;
        Result.OpenStatus = ELDShopOpenStatus::Rejected;
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::DataNotReady);
        ClientShopOpenResult(Result);
        return;
    }
    ClientShopOpenResult(Mode->Vendor->QueueShopOpen(Contender, Command));
}

void ALDPlayerController::ServerQueryGrowthReceipt_Implementation(const FLDReceiptQuery& Query)
{
    ALDGameMode* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
    if (Query.Family == ELDGrowthFamily::Shop)
    {
        FLDShopReceiptQueryResult Result;
        Result.Query = Query;
        Result.Lookup = ELDReceiptLookup::InvalidContext;
        ClientShopReceipt(Mode && Mode->Vendor && Contender.IsValid() ? Mode->Vendor->QueryShopReceipt(Contender, Query) : Result);
        return;
    }
    FLDChoiceReceiptQueryResult Result;
    Result.Query = Query;
    Result.Lookup = ELDReceiptLookup::InvalidContext;
    ClientChoiceReceipt(Query.Family == ELDGrowthFamily::Choice && Mode && Mode->Development && Contender.IsValid()
        ? Mode->Development->QueryChoiceReceipt(Contender, Query) : Result);
}

void ALDPlayerController::ClientGrowthSubmit_Implementation(const FLDSubmitResult& Result)
{
    if (LDGrowthWire::IsKnownFamily(Result.Family))
    {
        SettleGrowthRequest(Result.Family == ELDGrowthFamily::Shop ? ShopCursor : ChoiceCursor, Result.Generation,
            Result.RequestId, Result.bSequenceReserved, Result.Status == ELDAdmissionStatus::Final);
    }
    OnGrowthSubmit.Broadcast(Result);
}

void ALDPlayerController::ClientChoiceReceipt_Implementation(const FLDChoiceReceiptQueryResult& Result)
{
    if (Result.Lookup == ELDReceiptLookup::Final || Result.Lookup == ELDReceiptLookup::ReceiptExpired)
    {
        SettleGrowthRequest(ChoiceCursor, Result.Query.Generation, Result.Query.RequestId, true, true);
    }
    OnChoiceReceipt.Broadcast(Result);
}

void ALDPlayerController::ClientShopReceipt_Implementation(const FLDShopReceiptQueryResult& Result)
{
    if (Result.Lookup == ELDReceiptLookup::Final || Result.Lookup == ELDReceiptLookup::ReceiptExpired)
    {
        SettleGrowthRequest(ShopCursor, Result.Query.Generation, Result.Query.RequestId, true, true);
    }
    OnShopReceipt.Broadcast(Result);
}

void ALDPlayerController::ClientShopOpenResult_Implementation(const FLDShopOpenResult& Result)
{
    OnShopOpenResult.Broadcast(Result);
}
