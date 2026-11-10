// Playtest log of one game process (Framework/LDPlaytestTrace.h; docs/DEVELOPMENT.md, playtest troubleshooting) and the
// mark key of ALDPlayerController. The own-view lines of a remote client read only that player's own projections,
// room-public views and current authorized frame; the Host's authority record lives in LDGameModeTrace.cpp.
#include "Framework/LDPlaytestTrace.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDWorldViewRelay.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

bool LDPlaytest::InRunningMatch(uint32 Match, uint32 CurrentMatch, ELDMatchPhase Phase)
{
    return Match != 0 && Match == CurrentMatch && (Phase == ELDMatchPhase::Rest || Phase == ELDMatchPhase::Development
        || Phase == ELDMatchPhase::CaptureOpen);
}

namespace LDPlaytestTraceDetail
{
    const TCHAR* Role(const UWorld& World)
    {
        switch (World.GetNetMode())
        {
        case NM_ListenServer: return TEXT("host");
        case NM_Client: return TEXT("client");
        default: return TEXT("standalone");
        }
    }

    bool Running(const ALDGameState& Public, uint32 Match)
    {
        return LDPlaytest::InRunningMatch(Match, Public.PublicMatch.Generation, Public.PublicMatch.Phase);
    }

    // RTT, jitter, packet loss and receive silence of one connection, or why they are unavailable.
    FString ConnectionText(const UNetDriver& Driver, const UNetConnection& Connection)
    {
        if (Connection.GetLastRecvAckTime() <= 0) { return TEXT("rtt_ms=- jitter_ms=- loss_in=- loss_out=- since_recv_s=-"); }
        const double Ping = Connection.AvgLag > 0 ? Connection.AvgLag : Connection.RawPingInSeconds;
        return FString::Printf(TEXT("rtt_ms=%.0f jitter_ms=%.1f loss_in=%.1f loss_out=%.1f since_recv_s=%.2f"), Ping * 1000,
            Connection.GetAverageJitterInMS(), 100 * Connection.GetInLossPercentage().GetLossPercentage(),
            100 * Connection.GetOutLossPercentage().GetLossPercentage(), FMath::Max(0., Driver.GetElapsedTime() - Connection.LastReceiveTime));
    }

    FString NoticeAssisters(const FLDKillNotice& Notice)
    {
        FString Text;
        for (const FGuid& Assister : Notice.Assisters) { Text += (Text.IsEmpty() ? TEXT("") : TEXT(",")) + LDLog::Id(Assister); }
        return Text.IsEmpty() ? FString(TEXT("-")) : Text;
    }
}

FString FLDPlaytestTrace::OwnState(const ALDPlayerController& PC)
{
    const FLDOwnerState& Own = PC.OwnerState;
    int64 NowMs = -1;
    PC.ReadDemoAuthorityTimeMs(NowMs);
    const APawn* Body = PC.GetPawn();
    const FLDCombatOwnerView& Combat = PC.CombatView;
    const bool bCombat = Own.Life != 0 && Combat.bHasBody && Combat.Generation == Own.Match && Combat.Life == Own.Life;
    FString Resources = TEXT("hp=- mp=- shield=-"), Action = TEXT("-"), Statuses;
    if (bCombat)
    {
        Resources = FString::Printf(TEXT("hp=%s/%s mp=%s/%s shield=%s"), *LDLog::P2(Combat.HPP2), *LDLog::P2(Combat.MaxHPP2),
            *LDLog::P2(Combat.MPP2), *LDLog::P2(Combat.MaxMPP2), *LDLog::P2(Combat.ShieldP2));
        if (Combat.ActionPhase == ELDCombatPhase::Accepted) { Action = LDLog::EnumName(Combat.ActionEntry); }
        for (const FLDCombatStatusView& Status : Combat.Statuses)
        {
            Statuses += (Statuses.IsEmpty() ? TEXT("") : TEXT(",")) + LDLog::StatusName(Status.StatusId);
        }
    }
    const FLDProgressionView* Progression = PC.ReadOwnProgression();
    FString Order = TEXT("-");
    if (const ULDNavigationComponent* Navigation = PC.Navigation)
    {
        const FLDOrderReceipt& Receipt = Navigation->LatestReceipt();
        Order = Receipt.ReceiptSerial == 0 ? FString(TEXT("none"))
            : FString::Printf(TEXT("%s#%u:%s"), *LDLog::EnumName(Receipt.Kind), Receipt.Identity.Order, *LDLog::EnumName(Receipt.Status));
    }
    return FString::Printf(TEXT("t=%lld match=%u life=%u phase=%s pos=%s %s level=%d xp=%lld pending=%d order=%s action=%s statuses=%s"),
        NowMs, Own.Match, Own.Life, *LDLog::EnumName(Own.LifePhase), Body ? *LDLog::Cm(Body->GetActorLocation()) : TEXT("-"), *Resources,
        Progression ? int32(Progression->Level) : -1, Progression ? Progression->CumulativeXP : int64(-1),
        Progression ? int32(Progression->QueuedActiveOpportunities) : -1, *Order, *Action,
        Statuses.IsEmpty() ? TEXT("-") : *Statuses);
}

void FLDPlaytestTrace::Tick(ALDPlayerController& PC)
{
    const UWorld* World = PC.GetWorld();
    if (!World) { return; }
    const double Now = FPlatformTime::Seconds();
    TickPerformance(PC, Now);
    // The Listen Host's own player is fully covered by the authority heartbeat; only a remote client logs its own view.
    if (World->GetNetMode() == NM_Client) { TickSelf(PC, Now); }
}

void FLDPlaytestTrace::TickPerformance(ALDPlayerController& PC, double Now)
{
    const double Previous = LastFrameAt;
    LastFrameAt = Now;
    if (Previous < 0) { return; }
    const double Seconds = Now - Previous;
    const TCHAR* Role = LDPlaytestTraceDetail::Role(*PC.GetWorld());
    if (Seconds >= HitchSeconds)
    {
        uint32 Suppressed = 0;
        if (HitchLimit.Allow(Now, Suppressed))
        {
            int64 NowMs = -1;
            PC.ReadDemoAuthorityTimeMs(NowMs);
            UE_LOG(LogLDNet, Log, TEXT("LD_PERF_HITCH role=%s frame_ms=%.1f t=%lld suppressed=%u"), Role, Seconds * 1000, NowMs, Suppressed);
        }
    }
    if (!Frames.AddFrame(Seconds)) { return; }
    UE_LOG(LogLDNet, Log, TEXT("LD_PERF role=%s window_s=%.1f fps=%.1f frame_ms=%.2f peak_ms=%.1f frames=%u hitches=%u hitch_ms=%.0f"),
        Role, Frames.Frames * Frames.FrameMs / 1000, Frames.FPS, Frames.FrameMs, Frames.PeakMs, Frames.Frames, Frames.Hitches, HitchSeconds * 1000);
    LogConnections(PC);
}

void FLDPlaytestTrace::LogConnections(ALDPlayerController& PC)
{
    UWorld* World = PC.GetWorld();
    const UNetDriver* Driver = World->GetNetDriver();
    if (!Driver) { return; }
    if (World->GetNetMode() == NM_Client)
    {
        UNetConnection* Connection = Driver->ServerConnection.Get();
        if (!Connection) { return; }
        // Owner corrections of the current own body in this window (a new body starts its own count).
        uint32 Corrections = 0;
        double LastCm = 0, MaxCm = 0;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (const auto* Movement = Body ? Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr)
        {
            const bool bSameBody = OwnCorrectionsBody.Get() == Body;
            Corrections = bSameBody && Movement->CorrectionCount >= OwnCorrections ? Movement->CorrectionCount - OwnCorrections : Movement->CorrectionCount;
            OwnCorrections = Movement->CorrectionCount;
            OwnCorrectionsBody = const_cast<ALDCharacter*>(Body);
            LastCm = Movement->LastCorrectionCm;
            MaxCm = Movement->MaxCorrectionCm;
        }
        UE_LOG(LogLDNet, Log, TEXT("LD_NET role=client server=%s %s corrections=%u last_correction_cm=%.1f body_max_correction_cm=%.1f"),
            *LDLog::Quote(Connection->LowLevelGetRemoteAddress(true)), *LDPlaytestTraceDetail::ConnectionText(*Driver, *Connection),
            Corrections, LastCm, MaxCm);
        return;
    }
    const ALDGameMode* Mode = World->GetAuthGameMode<ALDGameMode>();
    for (UNetConnection* Connection : Driver->ClientConnections)
    {
        if (!Connection) { continue; }
        auto* Client = Cast<ALDPlayerController>(Connection->PlayerController);
        const FGuid Contender = Mode && Client ? Mode->ContenderId(Client) : FGuid();
        // Corrections this server sent to the client's current body in this window (a new body starts its own count).
        uint32 Corrections = 0;
        const ALDCharacter* Body = Client ? Cast<ALDCharacter>(Client->GetPawn()) : nullptr;
        if (const auto* Movement = Body ? Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr)
        {
            FBodyCorrections& Seen = ServerCorrections.FindOrAdd(TWeakObjectPtr<UObject>(Connection));
            const bool bSameBody = Seen.Body.Get() == Body;
            Corrections = bSameBody && Movement->ServerCorrectionCount >= Seen.Count
                ? Movement->ServerCorrectionCount - Seen.Count : Movement->ServerCorrectionCount;
            Seen.Body = const_cast<ALDCharacter*>(Body);
            Seen.Count = Movement->ServerCorrectionCount;
        }
        UE_LOG(LogLDNet, Log, TEXT("LD_NET role=host contender=%s address=%s %s corrections_sent=%u"), *LDLog::Id(Contender),
            *LDLog::Quote(Connection->LowLevelGetRemoteAddress(true)), *LDPlaytestTraceDetail::ConnectionText(*Driver, *Connection), Corrections);
    }
    for (auto It = ServerCorrections.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
}

void FLDPlaytestTrace::TickSelf(ALDPlayerController& PC, double Now)
{
    using namespace LDPlaytestTraceDetail;
    const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
    if (!Public) { return; }
    const FLDOwnerState& Own = PC.OwnerState;
    const bool bRunning = Running(*Public, Own.Match);
    if (bRunning && Own.ContenderId.IsValid() && JoinedMatch != Own.Match)
    {
        JoinedMatch = Own.Match;
        LogJoin(PC);
    }
    const FLDResultView& Result = Public->MatchResult;
    if (Own.Match != 0 && Result.Generation == Own.Match && ResultMatch != Own.Match && Result.ResultId.IsValid()
        && Result.ResultId == Public->PublicMatch.ResultId)
    {
        ResultMatch = Own.Match;
        UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_RESULT match=%u reason=%s winner=%s winner_name=%s duration_ms=%lld"), Result.Generation,
            *LDLog::EnumName(Result.Reason), *LDLog::Id(Result.Winner), *LDLog::Quote(LDHUD::RosterName(Public->DemoLobby, Result.Winner)),
            Result.DurationMs);
        for (const FLDResultRow& Row : Result.Rows)
        {
            UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_RESULT_ROW match=%u contender=%s name=%s kills=%llu deaths=%llu assists=%llu level=%u left=%d self=%d"),
                Result.Generation, *LDLog::Id(Row.ContenderId), *LDLog::Quote(Row.Name), Row.Kills, Row.Deaths, Row.Assists,
                uint32(Row.FinalLevel), Row.bLeft ? 1 : 0, Row.ContenderId == Own.ContenderId ? 1 : 0);
        }
    }
    if (Own.Match == 0 || Own.Match != Public->PublicMatch.Generation) { return; }
    // Every own-view line of a match follows its LD_SELF_MATCH line, so a reader can split the log by match.
    if (JoinedMatch != Own.Match) { return; }
    if (SeenLifeMatch != Own.Match || SeenLife != Own.Life || SeenPhase != uint8(Own.LifePhase))
    {
        SeenLifeMatch = Own.Match;
        SeenLife = Own.Life;
        SeenPhase = uint8(Own.LifePhase);
        int64 NowMs = -1;
        PC.ReadDemoAuthorityTimeMs(NowMs);
        UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_LIFE t=%lld match=%u life=%u phase=%s respawn_at_ms=%lld protected_until_ms=%lld"),
            NowMs, Own.Match, Own.Life, *LDLog::EnumName(Own.LifePhase), Own.RespawnAtMs, Own.ProtectedUntilMs);
    }
    if (!bDisplayContentTried)
    {
        // Only for cast display names; without content a cast reads by its stable id.
        bDisplayContentTried = true;
        FString Error;
        if (!LDDemoEntry::LoadRuntimeContent(DisplayContent, Error)) { DisplayContent.Reset(); }
    }
    LDWorldFeedback::FSample Sample;
    LDWorldFeedback::MakeSample(PC, *Public, DisplayContent ? &DisplayContent->Get() : nullptr, Sample);
    TArray<LDWorldFeedback::FEvent> Events;
    Feedback.Observe(Sample, Events);
    LogFeedback(PC, Events, Sample.Frame);
    LogStatuses(PC);
    LogKillFeed(PC);
    LogGrowth(PC);
    if (bRunning && UE_LOG_ACTIVE(LogLDTrace, Log) && StateGate.Due(int64(Now * 1000)))
    {
        UE_LOG(LogLDTrace, Log, TEXT("LD_SELF_STATE %s"), *OwnState(PC));
    }
}

void FLDPlaytestTrace::LogJoin(const ALDPlayerController& PC)
{
    const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
    const FLDOwnerState& Own = PC.OwnerState;
    FString Version, Error;
    if (!LDDemoEntry::ReadProductVersion(Version, Error)) { Version = TEXT("unknown"); }
    FString Commit;
    bool bDirtyKnown = false, bDirty = false;
    LDLog::SourceRevision(Commit, bDirtyKnown, bDirty);
    const UNetDriver* Driver = PC.GetWorld()->GetNetDriver();
    UNetConnection* Server = Driver ? Driver->ServerConnection.Get() : nullptr;
    UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_MATCH match=%u contender=%s name=%s product=%s wire=%u commit=%s server=%s"), Own.Match,
        *LDLog::Id(Own.ContenderId), *LDLog::Quote(Public ? LDHUD::RosterName(Public->DemoLobby, Own.ContenderId) : FString()), *Version,
        LDDemoEntry::WireProtocol, *Commit, Server ? *LDLog::Quote(Server->LowLevelGetRemoteAddress(true)) : TEXT("-"));
    if (!Public) { return; }
    // The public room roster, so a client-only log maps the ids of kill notices and visible units to names.
    for (int32 Slot = 0; Slot < Public->DemoLobby.Members.Num(); ++Slot)
    {
        const FLDLobbyMember& Member = Public->DemoLobby.Members[Slot];
        UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_ROSTER match=%u slot=%d contender=%s name=%s kind=%s host=%d self=%d"), Own.Match, Slot,
            *LDLog::Id(Member.ContenderId), *LDLog::Quote(Member.Name), Member.Kind == ELDContenderKind::Human ? TEXT("Human") : TEXT("AI"),
            Member.Host ? 1 : 0, Member.ContenderId == Own.ContenderId ? 1 : 0);
    }
}

void FLDPlaytestTrace::LogFeedback(const ALDPlayerController& PC, const TArray<LDWorldFeedback::FEvent>& Events, const FLDWorldView* Frame)
{
    using LDWorldFeedback::EKind;
    if (Events.IsEmpty()) { return; }
    int64 NowMs = -1;
    PC.ReadDemoAuthorityTimeMs(NowMs);
    const FLDCombatOwnerView& Combat = PC.CombatView;
    for (const LDWorldFeedback::FEvent& Event : Events)
    {
        switch (Event.Kind)
        {
        case EKind::DamageTaken:
        case EKind::Heal:
        case EKind::ShieldGain:
        case EKind::ShieldAbsorbed:
        {
            const TCHAR* Kind = Event.Kind == EKind::DamageTaken ? TEXT("damage") : Event.Kind == EKind::Heal ? TEXT("heal")
                : Event.Kind == EKind::ShieldGain ? TEXT("shield_gain") : TEXT("shield_absorbed");
            UE_LOG(LogLDCombat, Log, TEXT("LD_SELF_HP t=%lld kind=%s amount=%lld hp=%s/%s shield=%s"), NowMs, Kind, Event.Amount,
                *LDLog::P2(Combat.HPP2), *LDLog::P2(Combat.MaxHPP2), *LDLog::P2(Combat.ShieldP2));
            break;
        }
        case EKind::Experience:
            UE_LOG(LogLDGrowth, Log, TEXT("LD_SELF_REWARD t=%lld kind=xp amount=%lld"), NowMs, Event.Amount);
            break;
        case EKind::LevelUp:
            UE_LOG(LogLDGrowth, Log, TEXT("LD_SELF_LEVEL t=%lld level=%d choices_earned=%d points_earned=%lld"), NowMs, Event.Level,
                Event.Offers, Event.Amount);
            break;
        case EKind::Kill:
            UE_LOG(LogLDMatch, Log, TEXT("LD_SELF_KILL t=%lld victim_name=%s"), NowMs, *LDLog::Quote(Event.Name));
            break;
        case EKind::UnitCast:
        {
            // A cast this player saw: the visible unit's public identity and where it stood in the frame.
            const FLDObservedUnitView* Unit = Frame ? Frame->Units.FindByPredicate([&Event](const FLDObservedUnitView& Row)
                { return Row.Handle == Event.Handle && Row.ObservationEpoch == Event.Epoch; }) : nullptr;
            UE_LOG(LogLDCombat, Log, TEXT("LD_SELF_SEEN_CAST t=%lld cast=%s unit=%s pos=%s"), NowMs, *LDLog::Quote(Event.Name),
                !Unit ? TEXT("-") : Unit->Kind == ELDObservedUnitKind::Contender ? *LDLog::Id(Unit->ContenderId)
                    : *FString::Printf(TEXT("monster:%u"), uint32(Unit->CreatureId)),
                Unit ? *LDLog::Cm(Unit->FeetCm) : TEXT("-"));
            break;
        }
        default:
            break;
        }
    }
}

void FLDPlaytestTrace::LogStatuses(const ALDPlayerController& PC)
{
    const FLDOwnerState& Own = PC.OwnerState;
    const FLDCombatOwnerView& Combat = PC.CombatView;
    if (Own.Life == 0 || !Combat.bHasBody || Combat.Generation != Own.Match || Combat.Life != Own.Life) { return; }
    // A new life starts from no statuses, so its spawn protection reads as gained.
    if (StatusMatch != Own.Match || StatusLife != Own.Life)
    {
        StatusMatch = Own.Match;
        StatusLife = Own.Life;
        StatusRevision = 0;
        SeenStatuses.Reset();
    }
    if (Combat.Revision == StatusRevision) { return; }
    StatusRevision = Combat.Revision;
    TArray<uint8> Current;
    FString Added, Removed;
    bool bControlled = false;
    for (const FLDCombatStatusView& Status : Combat.Statuses)
    {
        Current.AddUnique(Status.StatusId);
        if (SeenStatuses.Contains(Status.StatusId)) { continue; }
        Added += FString::Printf(TEXT("%s%s:%lld"), Added.IsEmpty() ? TEXT("") : TEXT(","), *LDLog::StatusName(Status.StatusId),
            Status.ExpiresAtMs > 0 ? Status.ExpiresAtMs - Combat.AsOfMs : int64(-1));
        bControlled |= Status.ControlBlocks != 0;
    }
    for (const uint8 Status : SeenStatuses)
    {
        if (!Current.Contains(Status)) { Removed += (Removed.IsEmpty() ? TEXT("") : TEXT(",")) + LDLog::StatusName(Status); }
    }
    SeenStatuses = MoveTemp(Current);
    if (Added.IsEmpty() && Removed.IsEmpty()) { return; }
    UE_LOG(LogLDCombat, Log, TEXT("LD_SELF_STATUS t=%lld added=%s removed=%s controlled=%d"), Combat.AsOfMs,
        Added.IsEmpty() ? TEXT("-") : *Added, Removed.IsEmpty() ? TEXT("-") : *Removed, bControlled ? 1 : 0);
}

void FLDPlaytestTrace::LogKillFeed(const ALDPlayerController& PC)
{
    const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
    const FLDOwnerState& Own = PC.OwnerState;
    if (!Public || Public->KillFeed.Generation != Own.Match) { return; }
    if (FeedMatch != Own.Match)
    {
        FeedMatch = Own.Match;
        FeedSerial = 0;
    }
    for (const FLDKillNotice& Notice : Public->KillFeed.Notices)
    {
        if (Notice.Serial <= FeedSerial) { continue; }
        FeedSerial = Notice.Serial;
        const FString Killer = Notice.Killer.IsValid() ? LDHUD::RosterName(Public->DemoLobby, Notice.Killer) : FString();
        UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_KILLFEED serial=%llu t=%lld victim=%s victim_name=%s killer=%s killer_name=%s cause=%s assisters=%s"),
            Notice.Serial, Notice.OccurredAtMs, *LDLog::Id(Notice.Victim), *LDLog::Quote(LDHUD::RosterName(Public->DemoLobby, Notice.Victim)),
            *LDLog::Id(Notice.Killer), *LDLog::Quote(Killer), *LDLog::EnumName(Notice.Cause), *LDPlaytestTraceDetail::NoticeAssisters(Notice));
        if (Notice.Victim == Own.ContenderId)
        {
            UE_LOG(LogLDMatch, Display, TEXT("LD_SELF_DEATH t=%lld killer=%s killer_name=%s cause=%s assisters=%s"), Notice.OccurredAtMs,
                *LDLog::Id(Notice.Killer), *LDLog::Quote(Killer), *LDLog::EnumName(Notice.Cause), *LDPlaytestTraceDetail::NoticeAssisters(Notice));
        }
    }
}

void FLDPlaytestTrace::LogGrowth(const ALDPlayerController& PC)
{
    const FLDOwnerState& Own = PC.OwnerState;
    if (GrowthMatch != Own.Match)
    {
        GrowthMatch = Own.Match;
        SeenOffer.Invalidate();
        SeenBuildRevision = 0;
    }
    int64 NowMs = -1;
    PC.ReadDemoAuthorityTimeMs(NowMs);
    const FLDProgressionView* Progression = PC.ReadOwnProgression();
    if (Progression && Progression->bValid && Progression->Generation == Own.Match)
    {
        if (Progression->bHasActiveOffer && Progression->ActiveOffer.OfferId != SeenOffer)
        {
            SeenOffer = Progression->ActiveOffer.OfferId;
            const FString Cards = FString::JoinBy(Progression->ActiveOffer.Options, TEXT(","), [](FName Id) { return Id.ToString(); });
            UE_LOG(LogLDGrowth, Log, TEXT("LD_SELF_OFFER t=%lld ordinal=%u level=%u cards=%s"), NowMs,
                uint32(Progression->ActiveOffer.OpportunityOrdinal), uint32(Progression->Level), *Cards);
        }
        if (Progression->BuildRevision != SeenBuildRevision)
        {
            SeenBuildRevision = Progression->BuildRevision;
            FString Skills, Blueprints, Points;
            for (const FLDActiveSlotView& Active : Progression->Actives)
            {
                Skills += FString::Printf(TEXT("%s%s/%s"), Skills.IsEmpty() ? TEXT("") : TEXT(","), *Active.DefinitionId.ToString(),
                    Active.Slot == 0 ? TEXT("Q") : Active.Slot == 1 ? TEXT("W") : TEXT("E"));
            }
            for (const FName Id : Progression->PendingBlueprints)
            {
                Blueprints += FString::Printf(TEXT("%s%s"), Blueprints.IsEmpty() ? TEXT("") : TEXT(","), *Id.ToString());
            }
            for (const FLDStatPointsView& Stat : Progression->StatPoints)
            {
                Points += FString::Printf(TEXT("%s%s:%d"), Points.IsEmpty() ? TEXT("") : TEXT(","), *Stat.StatId.ToString(), Stat.Points);
            }
            UE_LOG(LogLDGrowth, Log, TEXT("LD_SELF_BUILD t=%lld level=%u xp=%lld pending=%u unspent=%lld skills=%s blueprints=%s points=%s"),
                NowMs, uint32(Progression->Level), Progression->CumulativeXP, uint32(Progression->QueuedActiveOpportunities),
                Progression->UnspentPoints, Skills.IsEmpty() ? TEXT("-") : *Skills, Blueprints.IsEmpty() ? TEXT("-") : *Blueprints,
                Points.IsEmpty() ? TEXT("-") : *Points);
        }
    }
}

uint32 FLDPlaytestTrace::Mark(ALDPlayerController& PC, bool bLogOwnView)
{
    ++Marks;
    if (bLogOwnView)
    {
        UE_LOG(LogLDMatch, Display, TEXT("LD_PLAYER_MARK source=self seq=%u contender=%s %s"), Marks, *LDLog::Id(PC.OwnerState.ContenderId),
            *OwnState(PC));
    }
    return Marks;
}

FLDPlaytestTrace& ALDPlayerController::Trace()
{
    if (!PlaytestTrace.IsValid()) { PlaytestTrace = MakePimpl<FLDPlaytestTrace>(); }
    return *PlaytestTrace;
}

void ALDPlayerController::PlaytestMark()
{
    if (!IsLocalController()) { return; }
    const ALDGameState* Public = GetWorld()->GetGameState<ALDGameState>();
    if (!Public || !LDPlaytestTraceDetail::Running(*Public, OwnerState.Match))
    {
        PostIntentNotice(TEXT("不在对局中，没有标记"));
        return;
    }
    int64 NowMs = -1;
    ReadDemoAuthorityTimeMs(NowMs);
    // A remote client writes the mark with its own view here; the Host's log gets it through the server either way.
    const uint32 Sequence = Trace().Mark(*this, GetNetMode() == NM_Client);
    ServerPlaytestMark(OwnerState.Match, Sequence, NowMs);
    PostIntentNotice(FString::Printf(TEXT("已标记 #%u（已写入试玩日志）"), Sequence));
}

void ALDPlayerController::ServerPlaytestMark_Implementation(uint32 Match, uint32 Sequence, int64 ClientAuthorityMs)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>()) { Mode->LogPlaytestMark(*this, Match, Sequence, ClientAuthorityMs); }
}
