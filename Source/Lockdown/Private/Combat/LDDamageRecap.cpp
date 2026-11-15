#include "Combat/LDDamageRecap.h"

namespace LDDamageRecapDetail
{
bool SourceLess(const FLDCombatSourceRef& A, const FLDCombatSourceRef& B)
{
    if (A.Kind!=B.Kind) { return uint8(A.Kind)<uint8(B.Kind); }
    return A.DefinitionId.LexicalLess(B.DefinitionId);
}

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
        return A.DamageP2!=B.DamageP2 ? A.DamageP2>B.DamageP2 : LDDamageRecapDetail::SourceLess(A.Source,B.Source);
    });
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
        if (Lines[Index].DamageP2<=0)
        {
            Error=TEXT("A damage line has no positive amount.");
            return false;
        }
        for (int32 Earlier=0; Earlier<Index; ++Earlier)
        {
            if (Lines[Earlier].SameKey(Lines[Index]))
            {
                Error=TEXT("A damage list repeats one attacker and source.");
                return false;
            }
        }
    }
    Error.Reset();
    return true;
}
