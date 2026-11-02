#pragma once

#include "CoreMinimal.h"
#include "LDMatchLifecycleTypes.generated.h"

struct FLDMatchContext
{
    uint32 Generation = 0;
    FGuid LedgerMatch;
    bool IsValid() const { return Generation != 0 && LedgerMatch.IsValid(); }
    bool operator==(const FLDMatchContext& Other) const { return Generation == Other.Generation && LedgerMatch == Other.LedgerMatch; }
    bool operator!=(const FLDMatchContext& Other) const { return !(*this == Other); }
};

UENUM()
enum class ELDMatchPhase : uint8
{
    Lobby, Starting, Development, CaptureOpen, Results, Resetting, Closing, Faulted
};

UENUM()
enum class ELDContenderKind : uint8 { Human, OpponentAI };

UENUM()
enum class ELDLifePhase : uint8 { Unspawned, Alive, Dead, Left };

UENUM()
enum class ELDVictimKind : uint8 { Contender, CampMember };

USTRUCT()
struct FLDVictimKey
{
    GENERATED_BODY()
    UPROPERTY() ELDVictimKind Kind = ELDVictimKind::Contender;
    UPROPERTY() FGuid EntityId;
    UPROPERTY() uint32 Life = 0;
    bool IsValid() const
    {
        return (Kind == ELDVictimKind::Contender || Kind == ELDVictimKind::CampMember)
            && EntityId.IsValid() && Life != 0;
    }
    bool operator==(const FLDVictimKey& Other) const
    { return Kind == Other.Kind && EntityId == Other.EntityId && Life == Other.Life; }
    bool operator!=(const FLDVictimKey& Other) const { return !(*this == Other); }
    friend uint32 GetTypeHash(const FLDVictimKey& Key)
    { return HashCombine(HashCombine(GetTypeHash(uint8(Key.Kind)), GetTypeHash(Key.EntityId)), GetTypeHash(Key.Life)); }
};

USTRUCT()
struct FLDCombatBodyRef
{
    GENERATED_BODY()
    UPROPERTY() FLDVictimKey Victim;
    UPROPERTY() FGuid BodyIdentity;
    bool IsValid() const { return Victim.IsValid() && BodyIdentity.IsValid(); }
    bool operator==(const FLDCombatBodyRef& Other) const
    { return Victim == Other.Victim && BodyIdentity == Other.BodyIdentity; }
    bool operator!=(const FLDCombatBodyRef& Other) const { return !(*this == Other); }
};

struct FLDDeathKey
{
    FGuid LedgerMatch;
    FLDVictimKey Victim;
    bool IsValid() const { return LedgerMatch.IsValid() && Victim.IsValid(); }
    bool operator==(const FLDDeathKey& Other) const { return LedgerMatch == Other.LedgerMatch && Victim == Other.Victim; }
    friend uint32 GetTypeHash(const FLDDeathKey& Key)
    { return HashCombine(GetTypeHash(Key.LedgerMatch), GetTypeHash(Key.Victim)); }
};

USTRUCT()
struct FLDDemoClockView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 StepSerial = 0;
    UPROPERTY() int64 NowMs = 0;
    bool IsValid() const { return Generation != 0 && StepSerial != 0 && NowMs >= 0; }
};

enum class ELDClockProjectionResult : uint8 { Accepted, Duplicate, Stale, Malformed };

enum class ELDCommitPhase : uint8
{
    Session, Motion, Actions, Effects, DeathsAndRewards, RespawnsAndWorld, Transactions, Capture, Close, Publish
};

enum class ELDCommitReject : uint8
{
    None, NotAuthority, InvalidRequest, StaleMatch, StaleLife, UnknownBody, WrongPhase,
    StaleStep, StepAlreadyCompleted, ScopeExpired, Reentrant, MatchClosed, TimeRegression,
    TimeUnavailable, CounterExhausted, AuthorityFault, GeometryUnavailable, CapacityExceeded
};

struct FLDContenderLifeRead
{
    ELDLifePhase Phase=ELDLifePhase::Unspawned;
    uint32 Life=0;
    uint64 LifeRevision=0;
    FLDCombatBodyRef Body;
};

struct FLDAuthorityStep
{
    FLDMatchContext Match;
    uint64 StepSerial = 0;
    double StartServerSeconds = 0;
    double EndServerSeconds = 0;
    bool operator==(const FLDAuthorityStep& Other) const
    {
        return Match == Other.Match && StepSerial == Other.StepSerial
            && StartServerSeconds == Other.StartServerSeconds && EndServerSeconds == Other.EndServerSeconds;
    }
};

struct FLDAuthorityBoundary
{
    FLDMatchContext Match;
    uint64 BatchSerial = 0;
    int64 AtMs = 0;
    bool operator==(const FLDAuthorityBoundary& Other) const
    { return Match == Other.Match && BatchSerial == Other.BatchSerial && AtMs == Other.AtMs; }
};

class ALDGameMode;

class FLDGameplayWritePermit
{
public:
    FLDGameplayWritePermit(const FLDGameplayWritePermit&) = delete;
    FLDGameplayWritePermit& operator=(const FLDGameplayWritePermit&) = delete;
    FLDGameplayWritePermit(FLDGameplayWritePermit&&) = delete;
    FLDGameplayWritePermit& operator=(FLDGameplayWritePermit&&) = delete;
    const FLDAuthorityBoundary& GetBoundary() const { return Boundary; }
    ELDCommitPhase GetPhase() const { return Phase; }
private:
    friend class ALDGameMode;
    FLDGameplayWritePermit(const ALDGameMode& InOwner, const FLDAuthorityBoundary& InBoundary,
        ELDCommitPhase InPhase, uint64 InGateRevision)
        : Owner(&InOwner), Boundary(InBoundary), Phase(InPhase), GateRevision(InGateRevision) {}
    const ALDGameMode* const Owner;
    const FLDAuthorityBoundary Boundary;
    const ELDCommitPhase Phase;
    const uint64 GateRevision;
};

UENUM()
enum class ELDSessionCommand : uint8 { SetReady, SetAICount, StartMatch, EndMatch, ReturnToLobby };

UENUM()
enum class ELDSessionOutcome : uint8 { Pending, Committed, Rejected };

UENUM()
enum class ELDSessionReject : uint8
{
    None, NotOwner, NotHost, WrongPhase, RoomFull, MatchAlreadyStarted, NotAllReady,
    StaleRoster, StaleMatch, RequestConflict, RequestInFlight, ReceiptExpired,
    InvalidRequest, ProtocolMismatch, ProductMismatch, ContentMismatch,
    MissingDemoDefinition, MatchClosed, AuthorityFault, SpawnBlocked
};

USTRUCT()
struct FLDSessionCommandKey
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 RequestSequence = 0;
    bool operator==(const FLDSessionCommandKey& Other) const { return Generation == Other.Generation && RequestSequence == Other.RequestSequence; }
};

USTRUCT()
struct FLDSessionRequest
{
    GENERATED_BODY()
    UPROPERTY() FLDSessionCommandKey Key;
    UPROPERTY() ELDSessionCommand Command = ELDSessionCommand::SetReady;
    UPROPERTY() uint64 ExpectedRosterRevision = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() uint8 AICount = 0;
    // ReturnToLobby only: the locked result the Host is leaving.
    UPROPERTY() FGuid ExpectedResult;
    bool operator==(const FLDSessionRequest& Other) const
    {
        return Key == Other.Key && Command == Other.Command && ExpectedRosterRevision == Other.ExpectedRosterRevision
            && Ready == Other.Ready && AICount == Other.AICount && ExpectedResult == Other.ExpectedResult;
    }
    bool HasValidShape() const
    {
        if (!Key.Generation || !Key.RequestSequence || !ExpectedRosterRevision) { return false; }
        switch (Command)
        {
        case ELDSessionCommand::SetReady: return AICount == 0 && !ExpectedResult.IsValid();
        case ELDSessionCommand::SetAICount: return !Ready && AICount <= 3 && !ExpectedResult.IsValid();
        case ELDSessionCommand::StartMatch:
        case ELDSessionCommand::EndMatch: return !Ready && AICount == 0 && !ExpectedResult.IsValid();
        case ELDSessionCommand::ReturnToLobby: return !Ready && AICount == 0 && ExpectedResult.IsValid();
        default: return false;
        }
    }
};

USTRUCT()
struct FLDSessionReceipt
{
    GENERATED_BODY()
    UPROPERTY() FLDSessionCommandKey Key;
    UPROPERTY() ELDSessionOutcome Outcome = ELDSessionOutcome::Rejected;
    UPROPERTY() ELDSessionReject Reason = ELDSessionReject::InvalidRequest;
    UPROPERTY() ELDMatchPhase Phase = ELDMatchPhase::Faulted;
    UPROPERTY() uint64 RosterRevision = 0;
    UPROPERTY() FString Detail;
};

USTRUCT()
struct FLDLobbyMember
{
    GENERATED_BODY()
    UPROPERTY() FGuid ContenderId;
    UPROPERTY() FString Name;
    UPROPERTY() ELDContenderKind Kind = ELDContenderKind::Human;
    UPROPERTY() bool Host = false;
    UPROPERTY() bool Ready = false;
};

USTRUCT()
struct FLDLobbyView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() ELDMatchPhase Phase = ELDMatchPhase::Faulted;
    UPROPERTY() uint64 RosterRevision = 0;
    UPROPERTY() uint64 PublicRevision = 0;
    UPROPERTY() TArray<FLDLobbyMember> Members;
    UPROPERTY() uint8 AICount = 0;
    UPROPERTY() bool CanStart = false;
    UPROPERTY() FString StartBlocker;
};

enum class ELDSessionWindowResult : uint8 { NewRequest, ExistingResult };
enum class ELDLobbyProjectionResult : uint8 { Accepted, Stale, Malformed };

class LOCKDOWN_API FLDSessionRequestWindow
{
public:
    ELDSessionWindowResult Begin(const FLDSessionRequest& Request, FLDSessionReceipt& Receipt);
    bool Complete(const FLDSessionReceipt& Receipt);
    uint64 ProcessedThrough() const { return LastSequence; }
    bool IsPending() const { return Pending; }
private:
    bool Pending = false;
    uint64 LastSequence = 0;
    FLDSessionRequest LastRequest;
    FLDSessionReceipt LastReceipt;
    FLDSessionRequest ActiveRequest;
};

namespace LDMatchLifecycle
{
    LOCKDOWN_API bool AllocateGeneration(uint32& LastGeneration, uint32& Generation);
    LOCKDOWN_API bool AuthoritySecondsToNowMs(double Seconds, int64& Out, FString& Error);
    LOCKDOWN_API bool AuthoritySecondsToEventMs(double Seconds, int64& Out, FString& Error);
    LOCKDOWN_API bool AddDurationMs(int64 StartMs, int64 DurationMs, int64& Out, FString& Error);
    LOCKDOWN_API bool DeadlineFromSeconds(double StartSeconds, int64 DurationMs, int64& Out, FString& Error);
    LOCKDOWN_API ELDClockProjectionResult AcceptClockView(const FLDDemoClockView& Candidate,
        FLDDemoClockView& Latest, FString& Error);
    LOCKDOWN_API ELDLobbyProjectionResult AcceptLobbyView(const FLDLobbyView& Candidate, FLDLobbyView& Latest, FString& Error);
}
