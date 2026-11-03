#include "Development/LDDevGrowthFixture.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool LDDevGrowthFixture::IsSwitchPresent()
{
    static const bool bSwitch = FParse::Param(FCommandLine::Get(), TEXT("LDDevGrowthFixture"));
    return bSwitch;
}

bool LDDevGrowthFixture::Admit(const UWorld* World, FString& Error)
{
    Error.Reset();
    if (!IsSwitchPresent())
    {
        Error = TEXT("The DEV growth fixture is off: the authority was not started with -LDDevGrowthFixture.");
        return false;
    }
    const ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!World || World->GetNetMode() == NM_Client || !Mode || !Mode->IsDemoRoom())
    {
        Error = TEXT("The DEV growth fixture acts only on the authority of a Demo room.");
        return false;
    }
    return true;
}

bool LDDevGrowthFixture::ResolvePreset(const TArray<LDProgression::FLegalOption>& Pool, const TArray<FName>& DefinitionIds,
    TArray<LDProgression::FLegalOption>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (DefinitionIds.Num() != 3)
    {
        Error = FString::Printf(TEXT("A preset offer names exactly three definitions, not %d."), DefinitionIds.Num());
        return false;
    }
    for (int32 Index = 0; Index < DefinitionIds.Num(); ++Index)
    {
        const FName Id = DefinitionIds[Index];
        const LDProgression::FLegalOption* Legal = Id.IsNone() ? nullptr
            : Pool.FindByPredicate([Id](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == Id; });
        if (!Legal)
        {
            Error = FString::Printf(TEXT("'%s' is not a legal choice of this build (unknown, at its rank cap or otherwise excluded)."),
                *Id.ToString());
        }
        else if (Out.ContainsByPredicate([Id](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == Id; }))
        {
            Error = FString::Printf(TEXT("'%s' appears twice; the three cards of an offer are distinct."), *Id.ToString());
        }
        if (!Error.IsEmpty())
        {
            Out.Reset();
            return false;
        }
        Out.Add(*Legal);
    }
    return true;
}

bool LDDevGrowthFixture::ResolvePreset(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
    const TArray<FName>& DefinitionIds, TArray<LDProgression::FLegalOption>& Out, FString& Error)
{
    Out.Reset();
    TArray<LDProgression::FLegalOption> Pool;
    const ELDGrowthReason PoolReason = LDProgression::LegalPool(Catalog, Build, Pool);
    if (PoolReason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The build has no legal pool: %s"), LDGrowthWire::ReasonName(PoolReason));
        return false;
    }
    return ResolvePreset(Pool, DefinitionIds, Out, Error);
}

ELDGrowthReason LDDevGrowthFixture::PickHead(const TArray<LDProgression::FLegalOption>& Pool, int64 Weight, uint64 Seed,
    TArray<TArray<FName>>& Queue, TArray<LDProgression::FLegalOption>& Out, bool& bPreset, FString& PresetError)
{
    Out.Reset();
    bPreset = false;
    PresetError.Reset();
    if (!Queue.IsEmpty())
    {
        const TArray<FName> Preset = MoveTemp(Queue[0]);
        Queue.RemoveAt(0);
        if (ResolvePreset(Pool, Preset, Out, PresetError))
        {
            bPreset = true;
            return ELDGrowthReason::None;
        }
    }
    return LDProgression::DrawThree(Pool, Weight, Seed, Out);
}
#endif
