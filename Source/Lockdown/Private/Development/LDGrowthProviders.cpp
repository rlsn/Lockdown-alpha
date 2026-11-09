#include "Development/LDGrowthDirector.h"

#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"

namespace
{
// Progression owns the roster setup of the growth domain and the frozen final levels.
FLDMatchProviderHooks GrowthProgressionHooks()
{
    FLDMatchProviderHooks Hooks;
    Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
    {
        if (!Mode.Growth)
        {
            Error = TEXT("The growth domain was not spawned for this match.");
            return false;
        }
        TArray<FGuid> Roster = Start.Humans;
        Roster.Append(Start.AISlots);
        return Mode.Growth->BeginMatch(Mode, Start.Match, Roster, Error);
    };
    Hooks.ReadFinalLevel = [](const ALDGameMode& Mode, const FGuid& Contender, uint8& Level, FString& Error)
    {
        if (!Mode.Growth)
        {
            Error = TEXT("The growth domain is not available for final levels.");
            return false;
        }
        return Mode.Growth->ReadFinalLevel(Contender, Level, Error);
    };
    return Hooks;
}

LDMatchProviders::FRegistrar GrowthProgressionRegistrar(ELDMatchProvider::Progression, &GrowthProgressionHooks);
}
