#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

// Normal start, AI admission and same-world return to a new Lobby generation.

FString ALDGameMode::StartGateBlocker() const
{
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry || !Registry->Enabled())
    {
        return TEXT("Demo 世界数据未加载，无法开始对局。");
    }
    const FString Missing = LDMatchProviders::MissingForStart(!LobbyAISlots.IsEmpty());
    if (!Missing.IsEmpty())
    {
        return FString::Printf(TEXT("当前版本缺少 Demo 玩法模块（%s），无法开始对局。"), *Missing);
    }
    return FString();
}

bool ALDGameMode::StartDemoMatch(ELDSessionReject& Reject, FString& Detail)
{
    // The caller verified Lobby, Host, current roster, readiness and the provider gate.
    Reject = ELDSessionReject::None;
    // Playtest log: everything the start commits from here (the opening three-choice offers, the AI registrations)
    // belongs to this generation; the header and roster follow once the roster is locked.
    UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_STARTING generation=%u ai=%d"), MatchContext.Generation, LobbyAISlots.Num());
    LockedAISlots = LobbyAISlots;
    MatchPhase = ELDMatchPhase::Starting;
    FString Error;
    auto Abort = [&](ELDSessionReject Why)
    {
        Reject = Why;
        // The player sees the Chinese text of the reject reason; the diagnostic stays in the authority log.
        Detail.Reset();
        UE_LOG(LogLDMatch, Warning, TEXT("LD_MATCH_START_ABORTED %s"), Error.IsEmpty() ? TEXT("unknown start failure.") : *Error);
        if (MatchPhase == ELDMatchPhase::Faulted)
        {
            Reject = ELDSessionReject::AuthorityFault;
            return false;
        }
        FString RetireError;
        if (!RetireMatch(true, RetireError))
        {
            FaultAuthority(RetireError);
            Reject = ELDSessionReject::AuthorityFault;
        }
        return false;
    };
    if (!InitializeAuthorityCore(Error)) { return Abort(ELDSessionReject::MissingDemoDefinition); }
    FLDMatchStartContext Start;
    Start.Match = MatchContext;
    Start.AISlots = LockedAISlots;
    Start.StartedAtMs = MatchStartedAtMs;
    Start.OpenAtMs = CaptureReducer.GetOpenAtMs();
    for (const FGuid& Id : RosterOrder)
    {
        if (IsMemberPresent(Id)) { Start.Humans.Add(Id); }
    }
    for (int32 Index = 0; Index < int32(ELDMatchProvider::Count); ++Index)
    {
        const FLDMatchProviderHooks* Hooks = LDMatchProviders::Find(ELDMatchProvider(Index));
        if (Hooks && Hooks->Prepare && !Hooks->Prepare(*this, Start, Error))
        {
            if (Error.IsEmpty())
            { Error = FString::Printf(TEXT("%s refused to prepare the match."), LDMatchProviders::Name(ELDMatchProvider(Index))); }
            return Abort(ELDSessionReject::MissingDemoDefinition);
        }
    }
    for (int32 Slot = 0; Slot < LockedAISlots.Num(); ++Slot)
    {
        if (!FindContender(LockedAISlots[Slot]))
        {
            Error = FString::Printf(TEXT("Opponent AI slot %d has no registered controller."), Slot + 1);
            return Abort(ELDSessionReject::MissingDemoDefinition);
        }
    }
    if (!ExecuteAuthorityPhase(*Combat, ELDCommitPhase::RespawnsAndWorld,
        [&](const FLDGameplayWritePermit& Permit) { return SpawnInitialContenders(Permit, Error); }, Error))
    {
        return Abort(ELDSessionReject::SpawnBlocked);
    }
    for (const FGuid& Id : RosterOrder)
    {
        if (IsMemberPresent(Id) && !CaptureTracks.Contains(Id))
        {
            Error = TEXT("A spawned contender has no committed capture join; the life provider is not wired.");
            return Abort(ELDSessionReject::MissingDemoDefinition);
        }
    }
    // #153: everyone is spawned; the opening countdown (moves and choices only, no attack or trade) runs to T=0 (MatchStartedAtMs), when development begins.
    MatchPhase = ELDMatchPhase::Countdown;
    bPublicMatchDirty = true;
    bKillFeedDirty = true;
    bLobbyDirty = true;
    PublishMatchOutcome();
    UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_STARTED generation=%u contenders=%d open_at_ms=%lld countdown_ms=%lld started_at_ms=%lld"),
        MatchContext.Generation, RosterOrder.Num(), CaptureReducer.GetOpenAtMs(), MatchStartedAtMs - AuthorityNowMs, MatchStartedAtMs);
    HeartbeatGate.Reset();
    LastMarkAt.Reset();
    LogPlaytestRoster();
    Detail = TEXT("对局开始。");
    return true;
}

void ALDGameMode::RetireAuthorityCore()
{
    for (auto& Pair : Lives)
    {
        if (auto* Body = Pair.Value.Actor.Get())
        {
            if (auto* Controller = Cast<ALDPlayerController>(Body->GetController()))
            {
                if (Controller->Navigation) { Controller->Navigation->ResetLife(); }
                Controller->UnPossess();
            }
            Body->Destroy();
        }
    }
    RetireMatchDomains();
    DemoContent.Reset();
    AuthorityStep = {};
    AuthorityBoundary = {};
    AuthorityOriginWorldSeconds = 0;
    LastWorldClockSample = 0;
    AuthorityNowSeconds = 0;
    AuthorityNowMs = 0;
    bAuthorityStepOpen = false;
    bAuthorityPhaseExecuted = false;
    bSettlementStep = false;
    LastAuthorityPhase = ELDCommitPhase::Session;
}

bool ALDGameMode::RetireMatch(bool bAbortedStart, FString& Error)
{
    if (!HasAuthority() || ActiveWritePermit || bAuthorityStepOpen)
    {
        Error = TEXT("A match cannot retire inside an authority step.");
        return false;
    }
    FLDMatchContext Next;
    auto* Instance = GetGameInstance<ULDGameInstance>();
    if (!Instance || !Instance->AllocateMatchGeneration(Next.Generation, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("No next match generation is available."); }
        return false;
    }
    Next.LedgerMatch = FGuid::NewGuid();
    if (GateRevision == MAX_uint64)
    {
        Error = TEXT("Write gate revision exhausted.");
        return false;
    }
    const FLDMatchContext Old = MatchContext;
    MatchPhase = ELDMatchPhase::Resetting;
    ++GateRevision;
    FLDMatchReset Reset;
    Reset.Old = Old;
    Reset.New = Next;
    Reset.bAbortedStart = bAbortedStart;
    for (int32 Index = int32(ELDMatchProvider::Count) - 1; Index >= 0; --Index)
    {
        const FLDMatchProviderHooks* Hooks = LDMatchProviders::Find(ELDMatchProvider(Index));
        if (Hooks && Hooks->MatchReset && !Hooks->MatchReset(*this, Reset, Error))
        {
            if (Error.IsEmpty())
            { Error = FString::Printf(TEXT("%s could not retire the old match."), LDMatchProviders::Name(ELDMatchProvider(Index))); }
            return false;
        }
    }
    RetireAuthorityCore();
    // Only connected humans stay in the room; AI records and contenders who left are dropped.
    for (auto It = Contenders.CreateIterator(); It; ++It)
    {
        FLDContenderRecord& Record = It.Value();
        if (Record.Kind != ELDContenderKind::Human || Record.bLeft || !It.Key().IsValid())
        {
            Lives.Remove(Record.Session);
            RosterOrder.Remove(Record.Session);
            It.RemoveCurrent();
            continue;
        }
        Lives.FindOrAdd(Record.Session) = FLDLifeRecord();
        FLDOwnerState Fresh;
        Fresh.Match = Next.Generation;
        Fresh.bLobbyHost = It.Key() == LobbyHost;
        Fresh.Revision = Record.State.Revision;
        Record.State = Fresh;
        Record.ReadyRosterRevision = 0;
        Record.SessionRequests = FLDSessionRequestWindow();
        Record.Kills = 0;
        Record.Deaths = 0;
        Record.Assists = 0;
    }
    for (FGuid& Slot : LobbyAISlots) { Slot = FGuid::NewGuid(); }
    LockedAISlots.Reset();
    CountedDeaths.Reset();
    KillNotices.Reset();
    NextKillSerial = 0;
    ResultView = {};
    bResultLocked = false;
    bResultPublished = false;
    MatchStartedAtMs = 0;
    MatchContext = Next;
    MatchPhase = ELDMatchPhase::Lobby;
    if (!AdvanceRoster(Error)) { return false; }
    bPublicMatchDirty = true;
    bLobbyDirty = true;
    PublishMatchOutcome();
    UE_LOG(LogLDMatch, Display, TEXT("LD_ROOM_RESET generation=%u aborted_start=%d"), MatchContext.Generation, bAbortedStart);
    Error.Reset();
    return true;
}

FLDAIRegistrationResult ALDGameMode::RegisterAIContender(AController& Controller, uint8 RosterSlot, const FLDMatchContext& Match)
{
    FLDAIRegistrationResult Result;
    auto Refuse = [&Result](ELDAIRegistration Outcome, const FString& Why)
    {
        Result.Outcome = Outcome;
        Result.Error = Why;
        return Result;
    };
    if (!HasAuthority() || MatchPhase != ELDMatchPhase::Starting || !bAuthorityInitialized)
    { return Refuse(ELDAIRegistration::WrongPhase, TEXT("Opponent AI registers only while the core starts a match.")); }
    if (Match != MatchContext) { return Refuse(ELDAIRegistration::StaleMatch, TEXT("The AI registration names another match.")); }
    if (RosterSlot >= LockedAISlots.Num()) { return Refuse(ELDAIRegistration::InvalidRosterSlot, TEXT("No frozen AI slot has this index.")); }
    auto* PC = Cast<ALDPlayerController>(&Controller);
    if (!PC || PC->Player || PC->GetWorld() != GetWorld())
    { return Refuse(ELDAIRegistration::InvalidController, TEXT("An opponent AI uses a server controller without a player or connection.")); }
    const FGuid Id = LockedAISlots[RosterSlot];
    if (const FLDContenderRecord* Existing = Contenders.Find(PC))
    {
        if (Existing->Session != Id || Existing->Kind != ELDContenderKind::OpponentAI)
        { return Refuse(ELDAIRegistration::AlreadyBound, TEXT("This controller is bound to another contender.")); }
        Result.Outcome = ELDAIRegistration::Registered;
        Result.Binding = FLDAIContenderBinding(MatchContext, Id);
        return Result;
    }
    if (FindContender(Id)) { return Refuse(ELDAIRegistration::AlreadyBound, TEXT("This AI slot is bound to another controller.")); }
    FLDContenderRecord Record;
    Record.Session = Id;
    Record.Kind = ELDContenderKind::OpponentAI;
    Record.Name = FString::Printf(TEXT("AI 对手 %d"), RosterSlot + 1);
    Record.State.Match = MatchContext.Generation;
    RosterOrder.Add(Id);
    Lives.Add(Id);
    Contenders.Add(PC, MoveTemp(Record));
    FString Error;
    if (Vision && !Vision->RegisterObserver(Id, PC, Error))
    {
        Contenders.Remove(PC);
        Lives.Remove(Id);
        RosterOrder.Remove(Id);
        return Refuse(ELDAIRegistration::InvalidController, Error);
    }
    Result.Outcome = ELDAIRegistration::Registered;
    Result.Binding = FLDAIContenderBinding(MatchContext, Id);
    return Result;
}

FLDAISelfReadResult ALDGameMode::ReadAISelf(const FLDAIContenderBinding& Binding) const
{
    FLDAISelfReadResult Result;
    if (!Binding.IsValid() || Binding.GetMatch() != MatchContext)
    {
        Result.Error = TEXT("The AI binding belongs to another match.");
        return Result;
    }
    for (const auto& Pair : Contenders)
    {
        if (Pair.Value.Session != Binding.GetContender()) { continue; }
        ALDPlayerController* PC = Pair.Key.Get();
        if (Pair.Value.Kind != ELDContenderKind::OpponentAI || !PC)
        {
            Result.Error = TEXT("The AI binding has no registered server controller.");
            return Result;
        }
        Result.Self = Pair.Value.State;
        if (!ProjectLife(*PC, Result.Self, Result.Error)) { return Result; }
        Result.Self.Match = MatchContext.Generation;
        Result.Succeeded = true;
        return Result;
    }
    Result.Error = TEXT("Unknown AI contender.");
    return Result;
}
