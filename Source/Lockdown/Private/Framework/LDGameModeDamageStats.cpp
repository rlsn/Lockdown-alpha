#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Combat/LDDamageRecap.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"

// Owner-only damage statistics. The authority only forwards what Combat committed: the recap frozen in a death fact and
// the per-source totals of the finished match. AI contenders get none; a list above the transport bound is not sent,
// so the owner shows the statistic as unavailable instead of a cut list.
void ALDGameMode::SendDeathRecap(const FLDCommittedDeath& Fact)
{
    ALDPlayerController* Controller = FindContenderController(Fact.Key.Victim.EntityId);
    if (!HasAuthority() || !Controller || Fact.Key.Victim.Kind != ELDVictimKind::Contender) { return; }
    if (Fact.RecapWindowMs <= 0 || Fact.Recap.Num() > LDDamageRecap::MaxLines)
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_DEATH_RECAP_SKIPPED contender=%s life=%u lines=%d window_ms=%lld"),
            *LDLog::Id(Fact.Key.Victim.EntityId), Fact.Key.Victim.Life, Fact.Recap.Num(), Fact.RecapWindowMs);
        return;
    }
    FLDDeathRecapView View;
    View.Match = MatchContext.Generation;
    View.Life = Fact.Key.Victim.Life;
    View.WindowMs = Fact.RecapWindowMs;
    View.Lines = Fact.Recap;
    // The killer is the fatal hit's contender credit (self-inflicted names nobody) or the camp member that landed it.
    if (Fact.CreditOwner.IsValid() && Fact.CreditOwner != Fact.VictimContender) { View.Killer = Fact.CreditOwner; }
    else { View.KillerMonsterId = Fact.KillerMonsterId; }
    View.Assisters = Fact.Assisters;
    int64 TotalP2 = 0;
    for (const FLDDamageLine& Line : View.Lines) { TotalP2 += Line.DamageP2; }
    UE_LOG(LogLDMatch, Display, TEXT("LD_DEATH_RECAP generation=%u contender=%s life=%u window_ms=%lld lines=%d damage_p2=%lld"),
        View.Match, *LDLog::Id(Fact.Key.Victim.EntityId), View.Life, View.WindowMs, View.Lines.Num(), TotalP2);
    Controller->ClientDeathRecap(View);
}

ELDReactionAssistDelivery ALDGameMode::SendReactionAssist(const FGuid& Setter, const FLDCombatBodyRef& ConsumedBody,
    const FLDCombatBodyRef& TriggerBody, const FLDReactionAssistView& View, FLDReactionAssistView& Sent, FString& Error)
{
    Sent = FLDReactionAssistView();
    Error.Reset();
    ALDPlayerController* Controller = FindContenderController(Setter);
    if (!Controller) { return ELDReactionAssistDelivery::Unavailable; }
    if (!HasAuthority() || !Vision || !Combat || View.Match != MatchContext.Generation)
    {
        Error = TEXT("Reaction prompt requires the current match, Combat and observation authority.");
        return ELDReactionAssistDelivery::AuthorityFault;
    }
    FLDContenderLifeRead Life;
    if (!ReadContenderLife(MatchContext, Setter, Life, Error)) { return ELDReactionAssistDelivery::AuthorityFault; }
    if (Life.Phase != ELDLifePhase::Alive || Life.Life == 0) { return ELDReactionAssistDelivery::Unavailable; }
    const auto CheckSight = [&](const FLDCombatBodyRef& Body)
    {
        FGuid Handle;
        uint32 Epoch = 0;
        if (!Body.IsValid() || !Vision->FindObservedHandle(*Controller, Body, Handle, Epoch))
        { return ELDObservedResolve::TargetUnavailable; }
        FLDAuthorityTargetSample Sample;
        const ELDObservedResolve Result = Vision->ResolvePresentedUnit(*Controller, Handle, Epoch, Sample, &Error);
        if (Result == ELDObservedResolve::Resolved && Sample.Target != Body)
        {
            Error = TEXT("The reaction prompt's observed handle resolved to a different body.");
            return ELDObservedResolve::AuthorityFault;
        }
        return Result;
    };
    const ELDObservedResolve TargetSight = CheckSight(ConsumedBody);
    if (TargetSight == ELDObservedResolve::AuthorityFault) { return ELDReactionAssistDelivery::AuthorityFault; }
    if (TargetSight != ELDObservedResolve::Resolved) { Error.Reset(); return ELDReactionAssistDelivery::Unavailable; }
    const ELDObservedResolve TriggerSight = TriggerBody.Victim.Kind == ELDVictimKind::Contender
        && TriggerBody.Victim.EntityId == View.Trigger ? CheckSight(TriggerBody) : ELDObservedResolve::TargetUnavailable;
    if (TriggerSight == ELDObservedResolve::AuthorityFault) { return ELDReactionAssistDelivery::AuthorityFault; }
    Sent = View;
    Sent.Life = Life.Life;
    if (TriggerSight != ELDObservedResolve::Resolved) { Sent.Trigger.Invalidate(); }
    Error.Reset();
    Controller->ClientReactionAssist(Sent);
    return ELDReactionAssistDelivery::Sent;
}

void ALDGameMode::PublishDamageSummaries()
{
    if (!HasAuthority() || !bResultLocked || !ResultView.ResultId.IsValid() || !Combat) { return; }
    // Spec §6.5: a chain still open at the close (the winning kill's among them) counts for 本局最离谱.
    FString ChainError;
    if (!Combat->FinishOpenChains(GetAuthorityNowMs(), ChainError))
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_CHAIN_FINISH_FAILED generation=%u %s"), MatchContext.Generation, *ChainError);
    }
    for (const auto& Pair : Contenders)
    {
        ALDPlayerController* Controller = Pair.Key.Get();
        const FLDContenderRecord& Contender = Pair.Value;
        if (!Controller || Contender.Kind != ELDContenderKind::Human || Contender.bLeft) { continue; }
        FLDDamageSummaryView View;
        View.Match = MatchContext.Generation;
        View.ResultId = ResultView.ResultId;
        if (!Combat->DamageTotalsOf(Contender.Session, View.Lines) || View.Lines.Num() > LDDamageRecap::MaxLines)
        {
            UE_LOG(LogLDMatch, Warning, TEXT("LD_DAMAGE_SUMMARY_SKIPPED generation=%u contender=%s lines=%d"),
                View.Match, *LDLog::Id(Contender.Session), View.Lines.Num());
            continue;
        }
        Combat->ReadResultHighlights(View.Highlights);
        int64 TotalP2 = 0;
        for (const FLDDamageLine& Line : View.Lines) { TotalP2 += Line.DamageP2; }
        UE_LOG(LogLDMatch, Display, TEXT("LD_DAMAGE_SUMMARY generation=%u contender=%s lines=%d damage_p2=%lld"),
            View.Match, *LDLog::Id(Contender.Session), View.Lines.Num(), TotalP2);
        Controller->ClientDamageSummary(View);
    }
}
