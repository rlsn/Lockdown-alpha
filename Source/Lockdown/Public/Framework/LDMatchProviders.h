#pragma once

#include "CoreMinimal.h"
#include "Framework/LDCaptureTypes.h"
#include "Vision/LDViewTypes.h"

class ALDGameMode;

// Gameplay providers that a normal Demo match needs. A provider registers once from its own
// translation unit (LDMatchProviders::FRegistrar); GameMode never includes provider headers.
enum class ELDMatchProvider : uint8 { Combat, Progression, Camps, Vision, OpponentAI, Count };

struct FLDMatchStartContext
{
    FLDMatchContext Match;
    // Locked start roster. AISlots[RosterSlot] is the contender id reserved in the Lobby.
    TArray<FGuid> Humans;
    TArray<FGuid> AISlots;
    int64 StartedAtMs = 0;
    int64 OpenAtMs = 0;
};

struct FLDMatchClosed
{
    FLDMatchContext Match;
    FGuid ResultId;
    ELDMatchEndReason Reason = ELDMatchEndReason::None;
    FGuid Winner;
    int64 EndedAtMs = 0;
};

struct FLDMatchReset
{
    FLDMatchContext Old;
    FLDMatchContext New;
    bool bAbortedStart = false;
};

enum class ELDLifeChange : uint8 { Joined, Died, Respawned, ProtectionEnded, Left };

struct FLDLifeNotice
{
    FLDMatchContext Match;
    FLDCombatBodyRef Body;
    ELDLifeChange Change = ELDLifeChange::Joined;
    uint64 LifeRevision = 0;
    int64 OccurredAtMs = 0;
};

struct FLDMatchProviderHooks
{
    // Starting only, before the initial spawn batch. Failure aborts the start: the room returns
    // to a new Lobby generation and every provider receives MatchReset with bAbortedStart.
    TFunction<bool(ALDGameMode&, const FLDMatchStartContext&, FString&)> Prepare;
    // After the unique result locked the write gate. Cancel/clean only; no permit exists.
    TFunction<void(ALDGameMode&, const FLDMatchClosed&)> MatchClosed;
    // Failed start or Results -> Lobby, called before the core retires bodies and the driver.
    // Drop every old-match actor, request window, cache and projection.
    TFunction<bool(ALDGameMode&, const FLDMatchReset&, FString&)> MatchReset;
    TFunction<void(ALDGameMode&, const FLDLifeNotice&)> LifeCommitted;
    // Progression only: committed final level for the frozen result rows.
    TFunction<bool(const ALDGameMode&, const FGuid& Contender, uint8& Level, FString&)> ReadFinalLevel;
};

namespace LDMatchProviders
{
    struct LOCKDOWN_API FRegistrar
    {
        FRegistrar(ELDMatchProvider Provider, FLDMatchProviderHooks (*Make)());
    };
    LOCKDOWN_API const TCHAR* Name(ELDMatchProvider Provider);
    LOCKDOWN_API const FLDMatchProviderHooks* Find(ELDMatchProvider Provider);
    // Providers a normal start still lacks, as a readable list; empty when the gate is satisfied.
    LOCKDOWN_API FString MissingForStart(bool bNeedsOpponentAI);
#if WITH_DEV_AUTOMATION_TESTS
    // Replaces the whole table for one rule test and restores it afterwards.
    struct LOCKDOWN_API FScopedTestTable
    {
        explicit FScopedTestTable(const TMap<ELDMatchProvider, FLDMatchProviderHooks>& Hooks);
        ~FScopedTestTable();
        FScopedTestTable(const FScopedTestTable&) = delete;
        FScopedTestTable& operator=(const FScopedTestTable&) = delete;
    };
#endif
}

// Server-issued AI binding. Only GameMode constructs a valid one; the AI adapter stores a copy.
class FLDAIContenderBinding
{
public:
    FLDAIContenderBinding() = default;
    const FLDMatchContext& GetMatch() const { return Match; }
    const FGuid& GetContender() const { return Contender; }
    bool IsValid() const { return Match.IsValid() && Contender.IsValid(); }
    bool operator==(const FLDAIContenderBinding& Other) const
    { return Match == Other.Match && Contender == Other.Contender; }
private:
    friend class ALDGameMode;
    FLDAIContenderBinding(const FLDMatchContext& InMatch, const FGuid& InContender)
        : Match(InMatch), Contender(InContender) {}
    FLDMatchContext Match;
    FGuid Contender;
};

enum class ELDAIRegistration : uint8 { Registered, WrongPhase, StaleMatch, InvalidRosterSlot, InvalidController, AlreadyBound };

struct FLDAIRegistrationResult
{
    ELDAIRegistration Outcome = ELDAIRegistration::WrongPhase;
    FLDAIContenderBinding Binding;
    FString Error;
};

struct FLDAISelfReadResult
{
    bool Succeeded = false;
    FString Error;
    FLDOwnerState Self;
};
