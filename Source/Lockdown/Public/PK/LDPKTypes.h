#pragma once

#include "CoreMinimal.h"
#include "LDPKTypes.generated.h"

// #198 session D: the PK inside a rest and its spectators (approved outer parameters, issue #198 comment 6752601175 §2-§4
// and §6). Public read model and request results; the authority is ALDPKDirector (PK/LDPKDirector.h), the pure rules are
// LDPKRules (PK/LDPKRules.h).

// The format drawn once after the roster locked: 2 entrants duel, 3 or 5 always free-for-all, 4 or 6 free-for-all or two
// equal random teams.
UENUM()
enum class ELDPKFormat : uint8 { None = 0, Duel = 1, FreeForAll = 2, Teams = 3 };

// Running from the PK start until a side is the last one alive (Won) or the deadline/a simultaneous wipe (Draw). NoContest:
// the arena could not be set up legally; nobody is paid.
UENUM()
enum class ELDPKOutcome : uint8 { None = 0, Pending = 1, Running = 2, Won = 3, Draw = 4, NoContest = 5 };

// Owner-sent result of a bet (ALDPlayerController::ServerPKBet -> ClientPKBet).
UENUM()
enum class ELDPKBetResult : uint8
{
    None = 0, Placed = 1, NotSpectator = 2, WindowClosed = 3, UnknownSide = 4, StaleMatch = 5, NotContender = 6, NoPK = 7
};

// Owner-sent result of a throw (ALDPlayerController::ServerPKThrow -> ClientPKThrow).
UENUM()
enum class ELDPKThrowResult : uint8
{
    None = 0, Thrown = 1, NotSpectator = 2, NotRunning = 3, CoolingDown = 4, OutsideArena = 5, OutOfRange = 6, NotGround = 7,
    StaleMatch = 8, NotContender = 9, NoPK = 10
};

// One locked entrant, in seat order (sorted contender id). Side is 1..SideCount; in a duel or a free-for-all every entrant
// is its own side.
USTRUCT()
struct LOCKDOWN_API FLDPKEntrantView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Contender;
    UPROPERTY() uint8 Side = 0;
    // Standing in the arena now (false before PKReady placed it, after it was eliminated, or after it forfeited).
    UPROPERTY() bool bAlive = false;
    // Left the match after the lock: eliminated at once, no participation or winner points.
    UPROPERTY() bool bForfeited = false;
};

// A spectator's public bet (one per eligible spectator, changeable until the betting step ends).
USTRUCT()
struct LOCKDOWN_API FLDPKBetView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Spectator;
    UPROPERTY() uint8 Side = 0;
};

// A throw in flight: its telegraphed ground point and landing instant.
USTRUCT()
struct LOCKDOWN_API FLDPKThrowView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() FVector TargetCm = FVector::ZeroVector;
    UPROPERTY() int64 LandsAtMs = 0;
};

// Points paid to one contender at the settlement of this PK (attribute points through the growth transaction path).
USTRUCT()
struct LOCKDOWN_API FLDPKAwardView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Contender;
    UPROPERTY() uint8 Participation = 0;
    UPROPERTY() uint8 Winner = 0;
    UPROPERTY() uint8 Bet = 0;
};

// Public, replicated read model of the current rest's PK (inside FLDPublicMatchView::PK). Serial is 0 when this rest has no
// PK (before the lock, or skipped); it is the match-wide PK number 1..4 otherwise. Ring and throw numbers come from the
// approved content parameters (FLDPKParameters), not from this view.
USTRUCT()
struct LOCKDOWN_API FLDPKView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() uint8 RestIndex = 0;
    UPROPERTY() ELDPKFormat Format = ELDPKFormat::None;
    UPROPERTY() uint8 SideCount = 0;
    UPROPERTY() TArray<FLDPKEntrantView> Entrants;
    // The spectators allowed to bet and throw (present non-entrants at the lock), sorted contender id.
    UPROPERTY() TArray<FGuid> Bettors;
    UPROPERTY() TArray<FLDPKBetView> Bets;
    // The ring centre (the capture anchor) and the fixed off-arena throw anchor of this match's world.
    UPROPERTY() FVector ArenaCentreCm = FVector::ZeroVector;
    UPROPERTY() FVector ThrowAnchorCm = FVector::ZeroVector;
    // Authority ms of the PK step's start (the ring clock's 0); 0 before it.
    UPROPERTY() int64 FightStartedAtMs = 0;
    UPROPERTY() ELDPKOutcome Outcome = ELDPKOutcome::None;
    UPROPERTY() uint8 WinnerSide = 0;
    UPROPERTY() int64 DecidedAtMs = 0;
    UPROPERTY() TArray<FLDPKThrowView> Throws;
    UPROPERTY() TArray<FLDPKAwardView> Awards;

    const FLDPKEntrantView* FindEntrant(const FGuid& Contender) const
    { return Entrants.FindByPredicate([&Contender](const FLDPKEntrantView& E) { return E.Contender == Contender; }); }
    bool IsBettor(const FGuid& Contender) const { return Bettors.Contains(Contender); }
};
