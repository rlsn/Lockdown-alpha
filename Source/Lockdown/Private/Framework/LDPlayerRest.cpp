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
    const auto* State = GetWorld() ? GetWorld()->GetGameState<ALDGameState>() : nullptr;
    if (!State) { OnTrainingReset.Broadcast(ELDTrainingResetResult::NotInRest); return; }
    const FLDTimelineView& View = State->PublicMatch.Timeline;
    ServerTrainingReset(OwnerState.Match, OwnerState.Life, View.RestIndex, View.StepStartedAtMs);
}

void ALDPlayerController::ServerTrainingReset_Implementation(uint32 Generation, uint32 Life, uint8 RestIndex, int64 StartedAtMs)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    ClientTrainingReset(Mode ? Mode->SubmitTrainingReset(*this, Generation, Life, RestIndex, StartedAtMs) : ELDTrainingResetResult::NotContender);
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

void ALDPlayerController::SubmitRestReady(bool bReady)
{
    const ALDGameState* State = GetWorld() ? GetWorld()->GetGameState<ALDGameState>() : nullptr;
    if (!State || OwnerState.Match == 0)
    {
        OnRestReady.Broadcast(ELDRestReadyResult::StaleMatch);
        return;
    }
    const FLDTimelineView& View = State->PublicMatch.Timeline;
    ServerRestReady(OwnerState.Match, View.RestIndex, View.StepStartedAtMs, bReady);
}

void ALDPlayerController::ServerRestReady_Implementation(uint32 Generation, uint8 RestIndex, int64 StartedAtMs, bool bReady)
{
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    ClientRestReady(Mode ? Mode->SubmitRestReady(*this, Generation, RestIndex, StartedAtMs, bReady) : ELDRestReadyResult::NotContender);
}

void ALDPlayerController::ClientRestReady_Implementation(ELDRestReadyResult Result)
{
    if (uint8(Result) > uint8(ELDRestReadyResult::NotOpen))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_REST_READY_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnRestReady.Broadcast(Result);
}