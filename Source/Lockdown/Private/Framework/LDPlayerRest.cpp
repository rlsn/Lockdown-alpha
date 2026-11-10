#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// #198 rest training reset: the owning connection's intent; the authority decides it in ALDGameMode::SubmitTrainingReset.
// The PK sign-up has no request: standing in the own sign-up disk is the sign-up (ALDGameMode::RefreshRestSignups).

void ALDPlayerController::SubmitTrainingReset()
{
    if (OwnerState.Match == 0)
    {
        OnTrainingReset.Broadcast(ELDTrainingResetResult::StaleMatch);
        return;
    }
    ServerTrainingReset(OwnerState.Match);
}

void ALDPlayerController::ServerTrainingReset_Implementation(uint32 Generation)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    ClientTrainingReset(Mode ? Mode->SubmitTrainingReset(*this, Generation) : ELDTrainingResetResult::NotContender);
}

void ALDPlayerController::ClientTrainingReset_Implementation(ELDTrainingResetResult Result)
{
    if (uint8(Result) > uint8(ELDTrainingResetResult::NoDummy))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_TRAINING_RESET_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnTrainingReset.Broadcast(Result);
}