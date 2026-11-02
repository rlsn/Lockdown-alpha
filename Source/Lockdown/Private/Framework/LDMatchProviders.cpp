#include "Framework/LDMatchProviders.h"

namespace
{
    constexpr int32 MatchProviderCount = int32(ELDMatchProvider::Count);

    struct FLDMatchProviderTable
    {
        FLDMatchProviderHooks (*Make[MatchProviderCount])() = {};
        TOptional<FLDMatchProviderHooks> Hooks[MatchProviderCount];
        bool bTestOverride = false;
        TOptional<FLDMatchProviderHooks> TestHooks[MatchProviderCount];
    };

    // Function-local static: registrars run during module static initialization.
    FLDMatchProviderTable& MatchProviderTable()
    {
        static FLDMatchProviderTable Instance;
        return Instance;
    }
}

LDMatchProviders::FRegistrar::FRegistrar(ELDMatchProvider Provider, FLDMatchProviderHooks (*Make)())
{
    const int32 Index = int32(Provider);
    if (Index < 0 || Index >= MatchProviderCount || !Make) { return; }
    // A Live Coding patch re-runs registrars; the newest code replaces the older hooks.
    MatchProviderTable().Make[Index] = Make;
    MatchProviderTable().Hooks[Index].Reset();
}

const TCHAR* LDMatchProviders::Name(ELDMatchProvider Provider)
{
    switch (Provider)
    {
    case ELDMatchProvider::Combat: return TEXT("Combat");
    case ELDMatchProvider::Progression: return TEXT("Progression");
    case ELDMatchProvider::Shop: return TEXT("Shop");
    case ELDMatchProvider::Camps: return TEXT("Camps");
    case ELDMatchProvider::Vision: return TEXT("Vision");
    case ELDMatchProvider::OpponentAI: return TEXT("OpponentAI");
    default: return TEXT("Unknown");
    }
}

const FLDMatchProviderHooks* LDMatchProviders::Find(ELDMatchProvider Provider)
{
    const int32 Index = int32(Provider);
    if (Index < 0 || Index >= MatchProviderCount) { return nullptr; }
    FLDMatchProviderTable& Providers = MatchProviderTable();
    if (Providers.bTestOverride) { return Providers.TestHooks[Index].GetPtrOrNull(); }
    if (!Providers.Make[Index]) { return nullptr; }
    if (!Providers.Hooks[Index]) { Providers.Hooks[Index] = Providers.Make[Index](); }
    return Providers.Hooks[Index].GetPtrOrNull();
}

FString LDMatchProviders::MissingForStart(bool bNeedsOpponentAI)
{
    TArray<FString> Missing;
    for (ELDMatchProvider Provider : {ELDMatchProvider::Combat, ELDMatchProvider::Progression,
        ELDMatchProvider::Shop, ELDMatchProvider::Camps, ELDMatchProvider::OpponentAI})
    {
        if (Provider == ELDMatchProvider::OpponentAI && !bNeedsOpponentAI) { continue; }
        const FLDMatchProviderHooks* Hooks = Find(Provider);
        const bool Complete = Hooks
            && (Provider != ELDMatchProvider::Progression || Hooks->ReadFinalLevel)
            && (Provider != ELDMatchProvider::OpponentAI || Hooks->Prepare);
        if (!Complete) { Missing.Add(Name(Provider)); }
    }
    return FString::Join(Missing, TEXT(", "));
}

#if WITH_DEV_AUTOMATION_TESTS
LDMatchProviders::FScopedTestTable::FScopedTestTable(const TMap<ELDMatchProvider, FLDMatchProviderHooks>& Hooks)
{
    FLDMatchProviderTable& Providers = MatchProviderTable();
    check(!Providers.bTestOverride);
    Providers.bTestOverride = true;
    for (auto& Slot : Providers.TestHooks) { Slot.Reset(); }
    for (const auto& Pair : Hooks)
    {
        const int32 Index = int32(Pair.Key);
        if (Index >= 0 && Index < MatchProviderCount) { Providers.TestHooks[Index] = Pair.Value; }
    }
}

LDMatchProviders::FScopedTestTable::~FScopedTestTable()
{
    FLDMatchProviderTable& Providers = MatchProviderTable();
    Providers.bTestOverride = false;
    for (auto& Slot : Providers.TestHooks) { Slot.Reset(); }
}
#endif
