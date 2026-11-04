#include "Combat/LDCombatDirector.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDGrowthProtocol.h"
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

bool ALDCombatDirector::DevApplyAttribute(TArray<FLDDemoStatBonus>& Bonuses, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount)
{
    if (bClearAll)
    {
        const bool bChanged = !Bonuses.IsEmpty();
        Bonuses.Reset();
        return bChanged;
    }
    const bool bRange = Stat == ELDDemoBonusStat::AttackRangeCmFlat;
    const int32 Index = Bonuses.IndexOfByPredicate([Stat](const FLDDemoStatBonus& Bonus) { return Bonus.Stat == Stat; });
    if (Amount == 0)
    {
        if (Index == INDEX_NONE)
        {
            return false;
        }
        Bonuses.RemoveAt(Index);
        return true;
    }
    FLDDemoStatBonus Term;
    Term.Stat = Stat;
    // Range is authored as a Cm rational; every other stat as an integer in its storage unit.
    if (bRange)
    {
        Term.Cm.Numerator = Amount;
        Term.Cm.Denominator = 1;
    }
    else
    {
        Term.Amount = Amount;
    }
    if (Index != INDEX_NONE)
    {
        FLDDemoStatBonus& Existing = Bonuses[Index];
        const bool bChanged = Existing.Amount != Term.Amount || Existing.Cm.Numerator != Term.Cm.Numerator
            || Existing.Cm.Denominator != Term.Cm.Denominator;
        Existing = Term;
        return bChanged;
    }
    const int32 At = Bonuses.IndexOfByPredicate([Stat](const FLDDemoStatBonus& Bonus) { return uint8(Bonus.Stat) > uint8(Stat); });
    Bonuses.Insert(Term, At == INDEX_NONE ? Bonuses.Num() : At);
    return true;
}

ELDGrowthReason ALDCombatDirector::DevSetAttribute(const FGuid& Contender, bool bClearAll, ELDDemoBonusStat Stat, int64 Amount,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    ALDGameMode* Mode = Core();
    const FLDDemoContentSnapshot* Content = Catalog();
    if (!CombatDevPermitValid(Mode, Permit, Detail) || !Content)
    {
        if (Detail.IsEmpty())
        {
            Detail = TEXT("A DEV attribute change needs the approved Demo content.");
        }
        return ELDGrowthReason::AuthorityFault;
    }
    FLDCombatEntityRecord* Record = FindRecord(Contender);
    if (!Record || Record->Kind != ELDVictimKind::Contender)
    {
        Detail = TEXT("The contender has no combat record yet (its first life has not spawned).");
        return ELDGrowthReason::DataInvalid;
    }
    FLDBuildProjectionInput Input = Record->Build;
    const auto Describe = [](const TArray<FLDDemoStatBonus>& Bonuses)
    {
        FString Text;
        for (const FLDDemoStatBonus& Bonus : Bonuses)
        {
            const FString Name = StaticEnum<ELDDemoBonusStat>()->GetNameStringByValue(int64(Bonus.Stat));
            Text += FString::Printf(TEXT("%s%s=%lld"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *Name,
                Bonus.Stat == ELDDemoBonusStat::AttackRangeCmFlat ? Bonus.Cm.Numerator : Bonus.Amount);
        }
        return Text.IsEmpty() ? FString(TEXT("empty")) : Text;
    };
    if (!DevApplyAttribute(Input.DevBonuses, bClearAll, Stat, Amount))
    {
        Detail = TEXT("dev layer unchanged: ") + Describe(Input.DevBonuses);
        return ELDGrowthReason::None;
    }
    FLDCombatPrepareResult Prepared = PrepareProjected(*Mode, *Content, *Record, Contender, Input, Record->Installed);
    if (!Prepared.IsSuccess())
    {
        Detail = Prepared.Message;
        return ELDGrowthReason::NumericDomain;
    }
    const uint64 SourceBefore = Record->SourceRevision;
    ApplyBuildChange(MoveTemp(Prepared.Value.GetValue()), Permit);
    if (Record->SourceRevision == SourceBefore)
    {
        Detail = TEXT("The DEV attribute install drifted inside its permit scope.");
        return ELDGrowthReason::AuthorityFault;
    }
    const FLDProjectedAttributes& Final = Record->Projection.Final;
    Detail = FString::Printf(TEXT("dev layer: %s; max hp %lld, max mp %lld, ad %lld, ap %lld, armor %lld, mr %lld P2"),
        *Describe(Record->Build.DevBonuses), Final.MaxHPP2, Final.MaxMPP2, Final.ADP2, Final.APP2, Final.ArmorP2,
        Final.MagicResistP2);
    return ELDGrowthReason::None;
}

FLDCombatPrepareResult ALDCombatDirector::DevPrepareReset(const FGuid& Contender, uint64 ResultBuildRevision,
    uint64 ResultInventoryRevision) const
{
    const ALDGameMode* Mode = Core();
    const FLDDemoContentSnapshot* Content = Catalog();
    const FLDCombatEntityRecord* Record = FindRecord(Contender);
    FLDCombatPrepareResult Refused;
    if (!Mode || !Content || !Record || Record->Kind != ELDVictimKind::Contender)
    {
        Refused.Error = ELDCombatPrepareError::UnknownContender;
        Refused.Message = TEXT("UnknownContender: no persistent Combat record yet (the first life has not spawned).");
        return Refused;
    }
    if (ResultBuildRevision <= Record->Installed.BuildRevision || ResultInventoryRevision < Record->Installed.InventoryRevision)
    {
        Refused.Error = ELDCombatPrepareError::InvalidInput;
        Refused.Message = TEXT("InvalidInput: the reset must advance the build revision and keep the inventory revision.");
        return Refused;
    }
    FLDBuildProjectionInput Input;
    Input.Level = 1;
    Input.ResultBuildRevision = ResultBuildRevision;
    Input.ResultInventoryRevision = ResultInventoryRevision;
    return PrepareProjected(*Mode, *Content, *Record, Contender, Input, Record->Installed);
}

int32 ALDCombatDirector::DevDropTracks(TArray<FLDCombatTrack>& Tracks, int64 NowMs)
{
    const int32 Dropped = Tracks.RemoveAll([](const FLDCombatTrack& Track) { return Track.Kind != ELDCombatSourceKind::Innate; });
    for (FLDCombatTrack& Track : Tracks)
    {
        Track.ReadyAtMs = FMath::Min(Track.ReadyAtMs, NowMs);
    }
    return Dropped;
}

int32 ALDCombatDirector::DevFinishReset(const FGuid& Contender, int64 NowMs)
{
    FLDCombatEntityRecord* Record = FindRecord(Contender);
    if (!Record)
    {
        return 0;
    }
    const int32 Dropped = DevDropTracks(Record->Tracks, NowMs);
    if (FLDBodyCombatState* Body = FindEntityBody(Contender); Body && !Body->bPendingDeath)
    {
        DevRefillPool(Body->Resources);
    }
    MarkViewChanged(Contender);
    return Dropped;
}
#endif
