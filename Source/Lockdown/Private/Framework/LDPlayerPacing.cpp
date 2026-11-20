#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// P6 owner intent: the death screen's spawn choice (spec §10.4). The authority decides it in ALDGameMode
// (Framework/LDGameModePacing.cpp).

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
