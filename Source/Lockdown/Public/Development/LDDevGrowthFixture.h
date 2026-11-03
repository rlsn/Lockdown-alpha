#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDProgressionRules.h"

class FLDDemoContentSnapshot;
class UWorld;

// DEV-only growth fixture for owned-connection scenarios. It exists only in builds with automation tests, acts only on
// the authority of a process started with -LDDevGrowthFixture and has no RPC: a Host DEV probe calls
// ALDDevelopmentDirector::DevQueueOffer and ALDEconomyDirector::DevGrantReward directly on the game thread.
// It never changes how an offer is chosen or a reward is committed, and it is not a play mode.
enum class ELDDevGrantStatus : uint8
{
    Unknown,  // no grant with this ticket in the current match
    Pending,  // queued for the next Transactions phase
    Granted,  // committed through the reward composite
    Rejected, // refused by that commit; nothing changed
};

namespace LDDevGrowthFixture
{
    // The process switch -LDDevGrowthFixture, read once.
    LOCKDOWN_API bool IsSwitchPresent();
    // The switch is present and World is the authority of a Demo room. Error explains every refusal.
    LOCKDOWN_API bool Admit(const UWorld* World, FString& Error);
    // Exactly three distinct definitions, each of them the definition of one option in Pool; the option kind and ranks
    // follow that pool (unlearned -> Learn*, learned -> Upgrade*, attribute -> Attribute). Out keeps the given order.
    LOCKDOWN_API bool ResolvePreset(const TArray<LDProgression::FLegalOption>& Pool, const TArray<FName>& DefinitionIds,
        TArray<LDProgression::FLegalOption>& Out, FString& Error);
    // The same check against the legal pool of Build.
    LOCKDOWN_API bool ResolvePreset(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& Build,
        const TArray<FName>& DefinitionIds, TArray<LDProgression::FLegalOption>& Out, FString& Error);
    // One head draw: the oldest queued preset is consumed and used when it still resolves against Pool; otherwise
    // (no preset, or PresetError explains the dropped one) the ordinary seeded draw runs.
    LOCKDOWN_API ELDGrowthReason PickHead(const TArray<LDProgression::FLegalOption>& Pool, int64 Weight, uint64 Seed,
        TArray<TArray<FName>>& Queue, TArray<LDProgression::FLegalOption>& Out, bool& bPreset, FString& PresetError);
}
#endif
