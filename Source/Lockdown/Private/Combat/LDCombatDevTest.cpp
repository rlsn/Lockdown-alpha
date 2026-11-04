#include "Combat/LDCombatDirector.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDGameMode.h"

namespace
{
bool CombatDevPermitValid(const ALDGameMode* Mode, const FLDGameplayWritePermit& Permit, FString& Detail)
{
    if (!Mode || Permit.GetPhase() != ELDCommitPhase::Transactions
        || Mode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None
        || Permit.GetBoundary().Match != Mode->GetMatchContext())
    {
        Detail = TEXT("A DEV test command needs the current Transactions permit.");
        return false;
    }
    return true;
}
}

bool ALDCombatDirector::DevRefillPool(FLDBodyCombatState::FResources& Pool)
{
    const bool bChanged = Pool.HpP2 != Pool.MaxHpP2 || Pool.MpP2 != Pool.MaxMpP2;
    Pool.HpP2 = Pool.MaxHpP2;
    Pool.MpP2 = Pool.MaxMpP2;
    if (bChanged)
    {
        ++Pool.Revision;
    }
    return bChanged;
}

int32 ALDCombatDirector::DevResetTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs)
{
    int32 Reset = 0;
    for (FLDCombatTrack& Track : Tracks)
    {
        if (Track.ReadyAtMs > NowMs)
        {
            // Only the end moves: identity, binding and the last duration stay, so a bound source keeps its track.
            Track.ReadyAtMs = NowMs;
            ++Reset;
        }
    }
    return Reset;
}

bool ALDCombatDirector::DevRefillResources(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    if (!CombatDevPermitValid(Core(), Permit, Detail))
    {
        return false;
    }
    const FLDCombatEntityRecord* Record = FindRecord(Contender);
    FLDBodyCombatState* Body = Record && Record->Kind == ELDVictimKind::Contender ? FindEntityBody(Contender) : nullptr;
    if (!Body || Body->bPendingDeath)
    {
        Detail = TEXT("The contender has no living combat body to refill.");
        return false;
    }
    const int64 HpBefore = Body->Resources.HpP2, MpBefore = Body->Resources.MpP2;
    if (DevRefillPool(Body->Resources))
    {
        MarkViewChanged(Contender);
    }
    Detail = FString::Printf(TEXT("hp %lld->%lld P2, mp %lld->%lld P2"), HpBefore, Body->Resources.HpP2, MpBefore,
        Body->Resources.MpP2);
    return true;
}

bool ALDCombatDirector::DevResetCooldowns(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    if (!CombatDevPermitValid(Core(), Permit, Detail))
    {
        return false;
    }
    FLDCombatEntityRecord* Record = FindRecord(Contender);
    if (!Record || Record->Kind != ELDVictimKind::Contender)
    {
        Detail = TEXT("The contender has no combat record.");
        return false;
    }
    const int32 Reset = DevResetTracks(Record->Tracks, Permit.GetBoundary().AtMs);
    if (Reset > 0)
    {
        MarkViewChanged(Contender);
    }
    Detail = FString::Printf(TEXT("%d of %d cooldown tracks were cooling down"), Reset, Record->Tracks.Num());
    return true;
}
#endif
