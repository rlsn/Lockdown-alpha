#include "Combat/LDAssistRules.h"

bool LDAssist::RecordContribution(TArray<FLDAssistContribution>& Contributions, const FGuid& Contributor,
    const FGuid& Victim, int64 NowMs)
{
    if (!Contributor.IsValid() || Contributor==Victim) { return false; }
    if (FLDAssistContribution* Existing=Contributions.FindByPredicate([&Contributor](const FLDAssistContribution& Row)
        { return Row.Contributor==Contributor; }))
    {
        if (NowMs<=Existing->LastAtMs) { return false; }
        Existing->LastAtMs=NowMs;
        return true;
    }
    Contributions.Add({Contributor,NowMs});
    return true;
}

void LDAssist::SelectAssisters(const TArray<FLDAssistContribution>& Contributions, const FGuid& Killer,
    const FGuid& Victim, int64 DeathAtMs, int64 WindowMs, TFunctionRef<bool(const FGuid&)> IsPresent, TArray<FGuid>& Out)
{
    Out.Reset();
    if (!Killer.IsValid() || WindowMs<=0) { return; }
    for (const FLDAssistContribution& Row : Contributions)
    {
        if (!Row.Contributor.IsValid() || Row.Contributor==Killer || Row.Contributor==Victim) { continue; }
        if (Row.LastAtMs>DeathAtMs || DeathAtMs-Row.LastAtMs>WindowMs) { continue; }
        if (!IsPresent(Row.Contributor)) { continue; }
        Out.AddUnique(Row.Contributor);
    }
    Out.Sort();
}

bool LDAssist::SplitKillReward(int64 BaseXP, int32 AssisterCount, FLDAssistRewardShares& Out)
{
    Out=FLDAssistRewardShares();
    if (BaseXP<0 || AssisterCount<0) { return false; }
    const int64 Sharers=int64(AssisterCount)+1;
    Out.KillerXP=BaseXP/Sharers;
    Out.AssisterXP=AssisterCount>0 ? BaseXP/Sharers : 0;
    return true;
}
