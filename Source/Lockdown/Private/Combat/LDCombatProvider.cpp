#include "Framework/LDMatchProviders.h"
#include "Framework/LDGameMode.h"

// Combat is a normal-start provider: at Close it ends live orders and unreleased actions, and on
// reset it drops each controller's order book, old-match owner projections and camp lives before the core
// retires the bodies and the per-match Combat director.
namespace LDCombatProviderDetail
{
    FLDMatchProviderHooks MakeCombatHooks()
    {
        FLDMatchProviderHooks Hooks;
        Hooks.MatchClosed=[](ALDGameMode& Mode, const FLDMatchClosed& Closed) { Mode.CloseCombatMatch(Closed); };
        Hooks.MatchReset=[](ALDGameMode& Mode, const FLDMatchReset& Reset, FString& Error)
        { return Mode.ResetCombatMatch(Reset,Error); };
        return Hooks;
    }

    const LDMatchProviders::FRegistrar CombatRegistrar(ELDMatchProvider::Combat,&MakeCombatHooks);
}
