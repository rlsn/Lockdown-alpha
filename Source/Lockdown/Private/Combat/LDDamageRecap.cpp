#include "Combat/LDDamageRecap.h"

namespace LDDamageRecapDetail
{
bool SourceLess(const FLDCombatSourceRef& A, const FLDCombatSourceRef& B)
{
    if (A.Kind!=B.Kind) { return uint8(A.Kind)<uint8(B.Kind); }
    return A.DefinitionId.LexicalLess(B.DefinitionId);
}

bool LineLess(const FLDDamageLine& A, const FLDDamageLine& B)
{
    if (A.Source!=B.Source) { return SourceLess(A.Source,B.Source); }
    if (A.Reaction!=B.Reaction) { return A.Reaction<B.Reaction; }
    if (A.Consumed!=B.Consumed) { return A.Consumed<B.Consumed; }
    return A.Setter<B.Setter;
}

// ELDCombatElement and ELDCombatReaction (Combat/LDCombatElements.h) both end at their third value.
constexpr uint8 LastElement=3;
constexpr uint8 LastReaction=3;

bool AttackerLess(const FLDDamageLine& A, const FLDDamageLine& B)
{
    if (A.Contender!=B.Contender) { return A.Contender<B.Contender; }
    return A.MonsterId.LexicalLess(B.MonsterId);
}
}

bool LDDamageRecap::Record(TArray<FLDDamageSample>& Log, const FLDDamageLine& Hit, int64 NowMs, int64 WindowMs)
{
    if (Hit.DamageP2<=0 || WindowMs<=0) { return false; }
    Log.RemoveAll([NowMs,WindowMs](const FLDDamageSample& Sample) { return Sample.AtMs>NowMs || NowMs-Sample.AtMs>WindowMs; });
    Log.Add({NowMs,Hit});
    return true;
}

void LDDamageRecap::Summarize(const TArray<FLDDamageSample>& Log, int64 DeathAtMs, int64 WindowMs, TArray<FLDDamageLine>& Out)
{
    Out.Reset();
    if (WindowMs<=0) { return; }
    // Each attacker's total over all its sources, for the attacker order.
    TArray<FLDDamageLine> Attackers;
    for (const FLDDamageSample& Sample : Log)
    {
        if (Sample.Line.DamageP2<=0 || Sample.AtMs>DeathAtMs || DeathAtMs-Sample.AtMs>WindowMs) { continue; }
        const FLDDamageLine& Hit=Sample.Line;
        if (FLDDamageLine* Row=Out.FindByPredicate([&Hit](const FLDDamageLine& Candidate) { return Candidate.SameKey(Hit); }))
        { Row->DamageP2+=Hit.DamageP2; }
        else { Out.Add(Hit); }
        if (FLDDamageLine* Total=Attackers.FindByPredicate([&Hit](const FLDDamageLine& Candidate) { return Candidate.SameAttacker(Hit); }))
        { Total->DamageP2+=Hit.DamageP2; }
        else { Attackers.Add(Hit); }
    }
    const auto TotalOf=[&Attackers](const FLDDamageLine& Line)
    {
        const FLDDamageLine* Total=Attackers.FindByPredicate([&Line](const FLDDamageLine& Candidate) { return Candidate.SameAttacker(Line); });
        return Total ? Total->DamageP2 : int64(0);
    };
    Out.Sort([&TotalOf](const FLDDamageLine& A, const FLDDamageLine& B)
    {
        if (!A.SameAttacker(B))
        {
            const int64 TotalA=TotalOf(A), TotalB=TotalOf(B);
            return TotalA!=TotalB ? TotalA>TotalB : LDDamageRecapDetail::AttackerLess(A,B);
        }
        return A.DamageP2!=B.DamageP2 ? A.DamageP2>B.DamageP2 : LDDamageRecapDetail::LineLess(A,B);
    });
}

void LDDamageRecap::HideUnreadableSetters(TArray<FLDDamageSample>& Log, int64 DeathAtMs, int64 WindowMs, const FGuid& Victim,
    const TArray<FGuid>& Assisters)
{
    TArray<FGuid> Readable=Assisters;
    if (Victim.IsValid()) { Readable.AddUnique(Victim); }
    for (const FLDDamageSample& Sample : Log)
    {
        if (Sample.Line.DamageP2>0 && Sample.AtMs<=DeathAtMs && DeathAtMs-Sample.AtMs<=WindowMs && Sample.Line.Contender.IsValid())
        { Readable.AddUnique(Sample.Line.Contender); }
    }
    for (FLDDamageSample& Sample : Log)
    {
        if (Sample.Line.Setter.IsValid() && !Readable.Contains(Sample.Line.Setter)) { Sample.Line.Setter.Invalidate(); }
    }
}

bool LDDamageRecap::AddTotal(TArray<FLDDamageLine>& Totals, const FLDCombatSourceRef& Source, int64 DamageP2)
{
    if (DamageP2<=0) { return false; }
    if (FLDDamageLine* Row=Totals.FindByPredicate([&Source](const FLDDamageLine& Candidate) { return Candidate.Source==Source; }))
    {
        Row->DamageP2+=DamageP2;
        return true;
    }
    FLDDamageLine& Row=Totals.AddDefaulted_GetRef();
    Row.Source=Source;
    Row.DamageP2=DamageP2;
    return true;
}

void LDDamageRecap::SortTotals(TArray<FLDDamageLine>& Totals)
{
    Totals.Sort([](const FLDDamageLine& A, const FLDDamageLine& B)
    { return A.DamageP2!=B.DamageP2 ? A.DamageP2>B.DamageP2 : LDDamageRecapDetail::SourceLess(A.Source,B.Source); });
}

bool LDDamageRecap::ValidateLines(const TArray<FLDDamageLine>& Lines, FString& Error)
{
    if (Lines.Num()>MaxLines)
    {
        Error=FString::Printf(TEXT("A damage list has %d lines, above the bound of %d."),Lines.Num(),MaxLines);
        return false;
    }
    for (int32 Index=0; Index<Lines.Num(); ++Index)
    {
        const FLDDamageLine& Line=Lines[Index];
        if (Line.DamageP2<=0)
        {
            Error=TEXT("A damage line has no positive amount.");
            return false;
        }
        if (Line.Consumed>LDDamageRecapDetail::LastElement || Line.Reaction>LDDamageRecapDetail::LastReaction
            || (Line.Consumed!=0 && Line.Reaction==0) || (Line.Setter.IsValid() && Line.Consumed==0))
        {
            Error=TEXT("A damage line has a malformed rule chain.");
            return false;
        }
        for (int32 Earlier=0; Earlier<Index; ++Earlier)
        {
            if (Lines[Earlier].SameKey(Lines[Index]))
            {
                Error=TEXT("A damage list repeats one attacker, source and rule chain.");
                return false;
            }
        }
    }
    Error.Reset();
    return true;
}

bool LDDamageRecap::ValidateReactionAssist(const FLDReactionAssistView& View, FString& Error)
{
    if (View.Match==0 || View.Life==0 || View.AtMs<0 || View.Consumed==0 || View.Consumed>LDDamageRecapDetail::LastElement
        || View.Reaction==0 || View.Reaction>LDDamageRecapDetail::LastReaction)
    {
        Error=TEXT("A reaction assist prompt has no match, life, element or reaction.");
        return false;
    }
    Error.Reset();
    return true;
}

LDDamageRecap::EAssistKeep LDDamageRecap::KeepReactionAssist(TArray<FLDReactionAssistView>& Kept, uint64& Serial,
    const FLDReactionAssistView& View, uint32 OwnMatch, uint32 OwnLife, int32 Cap, FString& Error)
{
    if (Cap<=0) { Error=TEXT("The assist prompt cap must be positive."); return EAssistKeep::Invalid; }
    if (!ValidateReactionAssist(View,Error)) { return EAssistKeep::Invalid; }
    auto Older=[](uint32 MatchA, uint32 LifeA, uint32 MatchB, uint32 LifeB)
    { return MatchA<MatchB || (MatchA==MatchB && LifeA<LifeB); };
    if (OwnMatch!=0 && (View.Match<OwnMatch || (View.Match==OwnMatch && OwnLife!=0 && View.Life<OwnLife)))
    {
        Error=TEXT("The assist prompt belongs to an earlier match or life of this player.");
        return EAssistKeep::Stale;
    }
    for (const FLDReactionAssistView& Each:Kept)
    {
        if (Older(View.Match,View.Life,Each.Match,Each.Life))
        {
            Error=TEXT("A newer match or life already has kept assist prompts.");
            return EAssistKeep::Stale;
        }
    }
    Kept.RemoveAll([&View,&Older](const FLDReactionAssistView& Each) { return Older(Each.Match,Each.Life,View.Match,View.Life); });
    FLDReactionAssistView& Added=Kept.Add_GetRef(View);
    Added.Serial=++Serial;
    if (Kept.Num()>Cap) { Kept.RemoveAt(0,Kept.Num()-Cap); }
    Error.Reset();
    return EAssistKeep::Kept;
}
