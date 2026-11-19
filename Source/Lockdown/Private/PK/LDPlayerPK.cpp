#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "PK/LDPKDirector.h"
#include "Engine/World.h"

// #198 PK spectators: the owning connection's bet and throw intents; the authority decides them in ALDPKDirector.

namespace
{
    ALDPKDirector* AuthorityPK(const ALDPlayerController& Controller, ALDGameMode*& OutMode)
    {
        OutMode = Controller.GetWorld() ? Controller.GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
        return OutMode ? OutMode->PKDirector.Get() : nullptr;
    }

    uint32 CurrentPKSerial(const ALDPlayerController& Controller)
    {
        const ALDGameState* State = Controller.GetWorld() ? Controller.GetWorld()->GetGameState<ALDGameState>() : nullptr;
        return State && State->PublicMatch.Generation == Controller.OwnerState.Match ? State->PublicMatch.PK.Serial : 0;
    }
}

void ALDPlayerController::SubmitPKBet(uint8 Side)
{
    const uint32 Serial = CurrentPKSerial(*this);
    if (OwnerState.Match == 0)
    {
        OnPKBet.Broadcast(ELDPKBetResult::StaleMatch);
        return;
    }
    if (Serial == 0)
    {
        OnPKBet.Broadcast(ELDPKBetResult::NoPK);
        return;
    }
    ServerPKBet(OwnerState.Match, Serial, Side);
}

void ALDPlayerController::SubmitPKThrow(const FVector& TargetCm)
{
    const uint32 Serial = CurrentPKSerial(*this);
    if (OwnerState.Match == 0 || TargetCm.ContainsNaN())
    {
        OnPKThrow.Broadcast(ELDPKThrowResult::StaleMatch, 0);
        return;
    }
    if (Serial == 0)
    {
        OnPKThrow.Broadcast(ELDPKThrowResult::NoPK, 0);
        return;
    }
    ServerPKThrow(OwnerState.Match, Serial, FVector_NetQuantize(TargetCm));
}

void ALDPlayerController::ServerPKBet_Implementation(uint32 Generation, uint32 Serial, uint8 Side)
{
    ALDGameMode* Mode = nullptr;
    ALDPKDirector* PK = AuthorityPK(*this, Mode);
    const FGuid Id = Mode ? Mode->ContenderId(this) : FGuid();
    ClientPKBet(!Mode || !Id.IsValid() ? ELDPKBetResult::NotContender
        : !PK ? ELDPKBetResult::NoPK : PK->SubmitBet(Id, Generation, Serial, Side));
}

void ALDPlayerController::ClientPKBet_Implementation(ELDPKBetResult Result)
{
    // Unknown bytes from the wire are refused, never reinterpreted.
    if (uint8(Result) > uint8(ELDPKBetResult::NoPK))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_PK_BET_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnPKBet.Broadcast(Result);
}

void ALDPlayerController::ServerPKThrow_Implementation(uint32 Generation, uint32 Serial, FVector_NetQuantize TargetCm)
{
    ALDGameMode* Mode = nullptr;
    ALDPKDirector* PK = AuthorityPK(*this, Mode);
    const FGuid Id = Mode ? Mode->ContenderId(this) : FGuid();
    int64 ReadyAtMs = 0;
    const ELDPKThrowResult Result = !Mode || !Id.IsValid() ? ELDPKThrowResult::NotContender
        : !PK ? ELDPKThrowResult::NoPK : PK->SubmitThrow(Id, Generation, Serial, TargetCm, ReadyAtMs);
    ClientPKThrow(Result, ReadyAtMs);
}

void ALDPlayerController::ClientPKThrow_Implementation(ELDPKThrowResult Result, int64 ReadyAtMs)
{
    if (uint8(Result) > uint8(ELDPKThrowResult::NoPK) || ReadyAtMs < 0)
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_PK_THROW_RESULT_MALFORMED value=%u"), uint32(Result));
        return;
    }
    OnPKThrow.Broadcast(Result, ReadyAtMs);
}
