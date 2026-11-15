#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Combat/LDDamageRecap.h"
#include "Framework/LDPlayerController.h"

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
    int64 TotalP2 = 0;
    for (const FLDDamageLine& Line : View.Lines) { TotalP2 += Line.DamageP2; }
    UE_LOG(LogLDMatch, Display, TEXT("LD_DEATH_RECAP generation=%u contender=%s life=%u window_ms=%lld lines=%d damage_p2=%lld"),
        View.Match, *LDLog::Id(Fact.Key.Victim.EntityId), View.Life, View.WindowMs, View.Lines.Num(), TotalP2);
    Controller->ClientDeathRecap(View);
}

void ALDGameMode::PublishDamageSummaries()
{
    if (!HasAuthority() || !bResultLocked || !ResultView.ResultId.IsValid() || !Combat) { return; }
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
        int64 TotalP2 = 0;
        for (const FLDDamageLine& Line : View.Lines) { TotalP2 += Line.DamageP2; }
        UE_LOG(LogLDMatch, Display, TEXT("LD_DAMAGE_SUMMARY generation=%u contender=%s lines=%d damage_p2=%lld"),
            View.Match, *LDLog::Id(Contender.Session), View.Lines.Num(), TotalP2);
        Controller->ClientDamageSummary(View);
    }
}
