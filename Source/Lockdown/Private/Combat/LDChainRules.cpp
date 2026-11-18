#include "Combat/LDChainRules.h"

void LDChain::Key(const TArray<FName>& Steps, TArray<FName>& Out)
{
    Out.Reset();
    for (const FName Step : Steps)
    {
        if (!Step.IsNone() && !Out.Contains(Step)) { Out.Add(Step); }
    }
}

bool LDChain::Counts(const TArray<FName>& Key, int32 MinSteps)
{
    return MinSteps > 0 && Key.Num() >= MinSteps;
}

FString LDChain::Text(const TArray<FName>& Key, const TCHAR* Separator)
{
    return FString::JoinBy(Key, Separator, [](FName Step) { return Step.ToString(); });
}

FName LDChain::ReactionStep(const TCHAR* ReactionName)
{
    return FName(*(FString(TEXT("reaction_")) + FString(ReactionName).ToLower()));
}

FName LDChain::GroundStep(const TCHAR* GroundName)
{
    return FName(*(FString(TEXT("ground_")) + FString(GroundName).ToLower()));
}

FName LDChain::KillStep()
{
    return FName(TEXT("kill"));
}

bool LDChain::KeepLongest(TArray<int32>& Longest, int32 Steps, int32 Capacity)
{
    if (Capacity <= 0 || Steps <= 0) { return false; }
    if (Longest.Num() >= Capacity && Steps <= Longest.Last()) { return false; }
    int32 At = 0;
    while (At < Longest.Num() && Longest[At] >= Steps) { ++At; }
    Longest.Insert(Steps, At);
    if (Longest.Num() > Capacity) { Longest.SetNum(Capacity); }
    return true;
}
