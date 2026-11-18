#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// P6 owner intents: the death screen's spawn choice (spec §10.4) and the vending machine (spec §11.3). The authority
// decides them in ALDGameMode (Framework/LDGameModePacing.cpp).

void ALDPlayerController::SubmitSpawnChoice(uint8 Index)
{
    if (OwnerState.Match == 0)
    {
        OnSpawnChoice.Broadcast(ELDSpawnChoiceResult::StaleMatch);
        return;
    }
    ServerChooseSpawn(OwnerState.Match, OwnerState.Life, Index);
}

void ALDPlayerController::ServerChooseSpawn_Implementation(uint32 Generation, uint32 Life, uint8 Index)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    ClientChooseSpawn(Mode ? Mode->SubmitSpawnChoice(*this, Generation, Life, Index) : ELDSpawnChoiceResult::NotContender);
}

void ALDPlayerController::ClientChooseSpawn_Implementation(ELDSpawnChoiceResult Result)
{
    if (uint8(Result) > uint8(ELDSpawnChoiceResult::OutOfRange))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_SPAWN_CHOICE_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnSpawnChoice.Broadcast(Result);
}

void ALDPlayerController::SubmitVending(ELDVendingItem Item, uint8 Element)
{
    if (OwnerState.Match == 0)
    {
        OnVending.Broadcast(ELDVendingResult::StaleMatch);
        return;
    }
    ServerVending(OwnerState.Match, OwnerState.Life, Item, Element);
}

void ALDPlayerController::ServerVending_Implementation(uint32 Generation, uint32 Life, ELDVendingItem Item, uint8 Element)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    ClientVending(Mode ? Mode->SubmitVending(*this, Generation, Life, Item, Element) : ELDVendingResult::NotContender);
}

void ALDPlayerController::ClientVending_Implementation(ELDVendingResult Result)
{
    if (uint8(Result) > uint8(ELDVendingResult::Busy))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_VENDING_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnVending.Broadcast(Result);
}
