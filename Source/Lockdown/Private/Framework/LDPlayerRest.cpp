#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// #198 rest PK sign-up: the owning connection's intent; the authority decides it in ALDGameMode::SubmitRestSignup.

void ALDPlayerController::SubmitRestSignup(bool bJoin)
{
    if (OwnerState.Match == 0)
    {
        OnRestSignup.Broadcast(ELDRestSignupResult::StaleMatch);
        return;
    }
    ServerRestSignup(OwnerState.Match, bJoin);
}

void ALDPlayerController::ServerRestSignup_Implementation(uint32 Generation, bool bJoin)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    const ELDRestSignupResult Result = Mode ? Mode->SubmitRestSignup(*this, Generation, bJoin) : ELDRestSignupResult::NotContender;
    ClientRestSignup(Result);
}

void ALDPlayerController::ClientRestSignup_Implementation(ELDRestSignupResult Result)
{
    // Unknown bytes from the wire are refused, never reinterpreted.
    if (uint8(Result) > uint8(ELDRestSignupResult::NotInRest))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_REST_SIGNUP_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnRestSignup.Broadcast(Result);
}

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