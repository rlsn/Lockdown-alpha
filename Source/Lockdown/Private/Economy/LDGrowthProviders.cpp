#include "Economy/LDEconomyDirector.h"

#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Inventory/LDVendorDirector.h"

namespace
{
// Progression owns the roster setup of the whole growth/shop domain and the frozen final levels.
FLDMatchProviderHooks GrowthProgressionHooks()
{
    FLDMatchProviderHooks Hooks;
    Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
    {
        if (!Mode.Economy)
        {
            Error = TEXT("The growth domain was not spawned for this match.");
            return false;
        }
        TArray<FGuid> Roster = Start.Humans;
        Roster.Append(Start.AISlots);
        return Mode.Economy->BeginMatch(Mode, Start.Match, Roster, Error);
    };
    Hooks.ReadFinalLevel = [](const ALDGameMode& Mode, const FGuid& Contender, uint8& Level, FString& Error)
    {
        if (!Mode.Economy)
        {
            Error = TEXT("The growth domain is not available for final levels.");
            return false;
        }
        return Mode.Economy->ReadFinalLevel(Contender, Level, Error);
    };
    return Hooks;
}

FLDMatchProviderHooks GrowthShopHooks()
{
    FLDMatchProviderHooks Hooks;
    Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
    {
        if (!Mode.Vendor || !Mode.Economy || Mode.Economy->GetMatch() != Start.Match)
        {
            Error = TEXT("The shop requires the growth domain prepared for this match.");
            return false;
        }
        return true;
    };
    return Hooks;
}

LDMatchProviders::FRegistrar GrowthProgressionRegistrar(ELDMatchProvider::Progression, &GrowthProgressionHooks);
LDMatchProviders::FRegistrar GrowthShopRegistrar(ELDMatchProvider::Shop, &GrowthShopHooks);
}
