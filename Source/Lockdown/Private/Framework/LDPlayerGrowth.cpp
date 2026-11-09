#include "Framework/LDPlayerController.h"

#include "Development/LDDevelopmentDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"

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
    const FGrowthRequestCursor& Cursor = ChoiceCursor;
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

void ALDPlayerController::QueryGrowthReceipt(const FLDReceiptQuery& Query)
{
    ServerQueryGrowthReceipt(Query);
}

const FLDProgressionView* ALDPlayerController::ReadOwnProgression() const
{
    const FLDProgressionView& View = OwnerState.Growth.Progression;
    return View.bValid && View.Generation == OwnerState.Match ? &View : nullptr;
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

void ALDPlayerController::ServerQueryGrowthReceipt_Implementation(const FLDReceiptQuery& Query)
{
    ALDGameMode* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
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
        SettleGrowthRequest(ChoiceCursor, Result.Generation,
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
