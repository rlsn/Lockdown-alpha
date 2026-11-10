#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDProgressionRules.h"

class FLDDemoContentSnapshot;
class UWorld;

// DEV-only growth fixture for owned-connection scenarios. It exists only in builds with automation tests, acts only on
// the authority of a process started with -LDDevGrowthFixture and has no RPC: a Host DEV probe calls
// LDDevGrowthFixture::StageOffer in the Lobby, ALDDevelopmentDirector::DevQueueOffer and ALDGrowthDirector::DevGrantReward
// in an open match, directly on the game thread.
// It never changes how an active is chosen or a reward is committed, and it is not a play mode.
// The same switch admits the owner-only DEV test commands (LDDevTestProtocol.h, ALDGrowthDirector::QueueDevTest): the
// fixture entries above stay Host-only, while a DEV test command reaches the authority through its owning connection's RPC
// and only ever acts on that connection's own contender.
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
    // One to three distinct actives that Build neither holds nor keeps as a pending blueprint, in the given order. A preset
    // is a deterministic DEV override and may name any such active, also one outside the opening pool.
    LOCKDOWN_API bool ResolvePreset(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build,
        const TArray<FName>& DefinitionIds, FString& Error);
    // One head draw: the oldest queued preset is consumed and used when it still resolves against Build; otherwise (no
    // preset, or PresetError explains the dropped one) the ordinary seeded draw runs (LDProgression::DrawActiveOffer).
    LOCKDOWN_API ELDGrowthReason PickHead(const FLDDemoContentDefinitions& Content, const FLDGrowthBuild& Build, uint8 Ordinal,
        uint64 Seed, TArray<TArray<FName>>& Queue, TArray<FName>& Out, bool& bPreset, FString& PresetError);
    // Lobby only: the growth domain does not exist before Start, so the fixture itself holds offers staged for the next
    // match's first heads (the opening offer first). The contender must be a member of World's Lobby; the actives are
    // checked against the empty starting build and each draw re-resolves them. Process-wide store on the Host authority.
    LOCKDOWN_API bool StageOffer(const UWorld* World, const FGuid& Contender, const TArray<FName>& DefinitionIds, FString& Error);
    // BeginMatch: moves the staged offers of Roster into Out and drops every other staged offer (logged as refused).
    LOCKDOWN_API void TakeStaged(const TArray<FGuid>& Roster, TMap<FGuid, TArray<TArray<FName>>>& Out);
}
#endif
