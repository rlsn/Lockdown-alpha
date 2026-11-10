// Host playtest log (docs/DEVELOPMENT.md, playtest troubleshooting): the match header and roster, the once-per-second
// heartbeat of every contender, the remote players' marks and the end-of-match summary. The Host may record complete
// authority information here; nothing in this file runs on a client or is replicated.
#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDPlaytestTrace.h"
#include "Navigation/LDNavigationComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

namespace LDGameModeTraceDetail
{
    FString HumanAddress(const ALDPlayerController& Controller)
    {
        if (Controller.IsLocalController()) { return TEXT("local"); }
        UNetConnection* Connection = Controller.GetNetConnection();
        return Connection ? Connection->LowLevelGetRemoteAddress(true) : FString(TEXT("-"));
    }

    // #198: Q/W/E actives with their slots, pending blueprints, the allocated points per stat (catalog order) and (#198 B)
    // the owned components, installed ones marked with '+' and their binding.
    FString BuildText(const FLDGrowthBuild& Build)
    {
        FString Skills, Blueprints, Components;
        for (const FLDLearnedActive& Active : Build.Actives)
        {
            Skills += FString::Printf(TEXT("%s%s/%s"), Skills.IsEmpty() ? TEXT("") : TEXT(","), *Active.Id.ToString(),
                Active.Slot == 0 ? TEXT("Q") : Active.Slot == 1 ? TEXT("W") : TEXT("E"));
        }
        for (const FName Id : Build.PendingBlueprints)
        {
            Blueprints += FString::Printf(TEXT("%s%s"), Blueprints.IsEmpty() ? TEXT("") : TEXT(","), *Id.ToString());
        }
        for (const FLDComponentInstance& Instance : Build.Components)
        {
            Components += FString::Printf(TEXT("%s%s%s%s"), Components.IsEmpty() ? TEXT("") : TEXT(","), Instance.bInstalled ? TEXT("+") : TEXT(""),
                *Instance.Id.ToString(), Instance.Target.BoundActive.IsNone() ? TEXT("")
                    : *FString::Printf(TEXT("@%s:%s"), *Instance.Target.BoundActive.ToString(), *LDLog::EnumName(Instance.Target.Element)));
        }
        const FString Points = FString::JoinBy(Build.StatPoints, TEXT(","), [](int32 Count) { return FString::FromInt(Count); });
        return FString::Printf(TEXT("skills=%s blueprints=%s points=%s components=%s"), Skills.IsEmpty() ? TEXT("-") : *Skills,
            Blueprints.IsEmpty() ? TEXT("-") : *Blueprints, Points.IsEmpty() ? TEXT("-") : *Points,
            Components.IsEmpty() ? TEXT("-") : *Components);
    }
}

void ALDGameMode::LogPlaytestRoster() const
{
    using namespace LDGameModeTraceDetail;
    const UWorld* World = GetWorld();
    UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
    FString Version, VersionError;
    if (!LDDemoEntry::ReadProductVersion(Version, VersionError)) { Version = TEXT("unknown"); }
    FString Commit;
    bool bDirtyKnown = false, bDirty = false;
    LDLog::SourceRevision(Commit, bDirtyKnown, bDirty);
    bool bDevTest = false, bDevReload = false;
    const FLDLobbyView Lobby = ReadLobby();
    bDevTest = Lobby.DevTestSession;
    bDevReload = Lobby.DevContentReload;
    UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_HEADER generation=%u ledger=%s product=%s wire=%u commit=%s dirty=%s catalog=%s net=%s address=%s contenders=%d ai=%d open_at_ms=%lld dev_test=%d dev_reload=%d"),
        MatchContext.Generation, *LDLog::Id(MatchContext.LedgerMatch), *Version, LDDemoEntry::WireProtocol, *Commit,
        bDirtyKnown ? (bDirty ? TEXT("1") : TEXT("0")) : TEXT("unknown"), EntryCatalogDigest.IsEmpty() ? TEXT("-") : *EntryCatalogDigest,
        GetNetMode() == NM_ListenServer ? TEXT("ListenServer") : GetNetMode() == NM_Standalone ? TEXT("Standalone") : TEXT("Other"),
        Driver ? *LDLog::Quote(Driver->LowLevelGetNetworkNumber()) : TEXT("-"), RosterOrder.Num(), LockedAISlots.Num(),
        CaptureReducer.GetOpenAtMs(), bDevTest ? 1 : 0, bDevReload ? 1 : 0);
    const FString Character = DemoContent ? DemoContent->Get().Character.Id : FString(TEXT("-"));
    for (int32 Slot = 0; Slot < RosterOrder.Num(); ++Slot)
    {
        const FGuid& Id = RosterOrder[Slot];
        const ALDPlayerController* Controller = nullptr;
        const FLDContenderRecord* Record = nullptr;
        for (const auto& Pair : Contenders)
        {
            if (Pair.Value.Session == Id) { Controller = Pair.Key.Get(); Record = &Pair.Value; break; }
        }
        if (!Record) { continue; }
        const bool bHuman = Record->Kind == ELDContenderKind::Human;
        UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_ROSTER generation=%u slot=%d contender=%s name=%s kind=%s host=%d character=%s address=%s"),
            MatchContext.Generation, Slot, *LDLog::Id(Id), *LDLog::Quote(Record->Name), bHuman ? TEXT("Human") : TEXT("AI"),
            Controller && Controller == LobbyHost.Get() ? 1 : 0, *Character,
            !bHuman ? TEXT("ai") : Controller ? *LDLog::Quote(HumanAddress(*Controller)) : TEXT("-"));
    }
}

void ALDGameMode::LogPlaytestHeartbeat()
{
    // The gate and every read below are skipped while the trace category is off.
    if (!UE_LOG_ACTIVE(LogLDTrace, Log) || !Combat || !IsGameplayWriteOpen(MatchContext)) { return; }
    if (!HeartbeatGate.Due(AuthorityNowMs)) { return; }
    using namespace LDGameModeTraceDetail;
    for (const FGuid& Id : RosterOrder)
    {
        const FLDLifeRecord* Life = Lives.Find(Id);
        const FLDContenderRecord* Record = FindContender(Id);
        if (!Life || !Record) { continue; }
        FString Position = TEXT("-"), Resources = TEXT("hp=- mp=- shield=-"), Action = TEXT("-"), Statuses;
        if (const ALDCharacter* Body = Life->Actor.Get())
        {
            Position = LDLog::Cm(ALDCombatDirector::FootOf(*Body));
            FLDCombatBodyRead Read;
            if (Life->Phase == ELDLifePhase::Alive && Combat->ReadCombatBody(Life->Body, Read))
            {
                Resources = FString::Printf(TEXT("hp=%s/%s mp=%s/%s shield=%s"), *LDLog::P2(Read.HPP2), *LDLog::P2(Read.MaxHPP2),
                    *LDLog::P2(Read.MPP2), *LDLog::P2(Read.MaxMPP2), *LDLog::P2(Read.ShieldP2));
                if (Read.bActionInProgress) { Action = LDLog::EnumName(Read.ActionEntry); }
            }
            TArray<FLDObservedStatus> Observed;
            Combat->AppendObservedStatuses(*Body, AuthorityNowMs, Observed);
            for (const FLDObservedStatus& Status : Observed)
            {
                Statuses += (Statuses.IsEmpty() ? TEXT("") : TEXT(",")) + LDLog::StatusName(Status.StatusId);
            }
        }
        const FLDGrowthBuild* Build = Development ? Development->FindBuild(Id) : nullptr;
        const FLDProgressionState* Progression = Development ? Development->FindProgression(Id) : nullptr;
        FString Order = TEXT("-");
        FVector OrderPoint;
        bool bOrderPoint = false;
        if (const ALDPlayerController* Controller = FindContenderController(Id); Controller && Controller->Navigation)
        {
            Order = Controller->Navigation->DescribeAuthorityOrder(OrderPoint, bOrderPoint);
        }
        UE_LOG(LogLDTrace, Log, TEXT("LD_HEARTBEAT t=%lld contender=%s phase=%s life=%u pos=%s %s level=%d xp=%lld pending=%d order=%s order_at=%s action=%s statuses=%s protected_until=%lld respawn_at=%lld"),
            AuthorityNowMs, *LDLog::Id(Id), *LDLog::EnumName(Life->Phase), Life->Life, *Position, *Resources,
            Build ? int32(Build->Level) : -1, Progression ? Progression->CumulativeXP : int64(-1),
            Progression ? int32(Progression->QueuedActiveOpportunities()) : -1, *Order, bOrderPoint ? *LDLog::Cm(OrderPoint) : TEXT("-"), *Action,
            Statuses.IsEmpty() ? TEXT("-") : *Statuses, Life->ProtectedUntilMs, Life->Phase == ELDLifePhase::Dead ? Life->RespawnAtMs : int64(0));
    }
}

void ALDGameMode::LogPlaytestMark(ALDPlayerController& Controller, uint32 Match, uint32 Sequence, int64 ClientAuthorityMs)
{
    const FLDContenderRecord* Record = Contenders.Find(&Controller);
    if (!Record) { return; }
    // A remote mark costs one log line; a flood from one connection is cut to one per second.
    const double Now = FPlatformTime::Seconds();
    double& Last = LastMarkAt.FindOrAdd(Record->Session, -1.);
    if (Last >= 0 && Now - Last < 1.) { return; }
    Last = Now;
    // A mark sent for an earlier match, or arriving outside the running match, never lands in the current match's record.
    if (!LDPlaytest::InRunningMatch(Match, MatchContext.Generation, MatchPhase))
    {
        UE_LOG(LogLDMatch, Log, TEXT("LD_PLAYER_MARK_REJECTED contender=%s seq=%u match=%u current=%u phase=%s"),
            *LDLog::Id(Record->Session), Sequence, Match, MatchContext.Generation, *LDLog::EnumName(MatchPhase));
        return;
    }
    const FLDLifeRecord* Life = Lives.Find(Record->Session);
    const ALDCharacter* Body = Life ? Life->Actor.Get() : nullptr;
    UE_LOG(LogLDMatch, Display, TEXT("LD_PLAYER_MARK source=host_view contender=%s name=%s seq=%u t=%lld client_t=%lld phase=%s pos=%s"),
        *LDLog::Id(Record->Session), *LDLog::Quote(Record->Name), Sequence, AuthorityNowMs, ClientAuthorityMs,
        Life ? *LDLog::EnumName(Life->Phase) : TEXT("-"), Body ? *LDLog::Cm(ALDCombatDirector::FootOf(*Body)) : TEXT("-"));
}

void ALDGameMode::LogPlaytestSummary(const FLDResultView& View) const
{
    using namespace LDGameModeTraceDetail;
    UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_SUMMARY generation=%u reason=%s winner=%s started_ms=%lld ended_ms=%lld duration_ms=%lld"),
        View.Generation, *LDLog::EnumName(View.Reason), *LDLog::Id(View.Winner), View.StartedAtMs, View.EndedAtMs, View.DurationMs);
    for (const FLDResultRow& Row : View.Rows)
    {
        const FLDGrowthBuild* Build = Development ? Development->FindBuild(Row.ContenderId) : nullptr;
        const FLDProgressionState* Progression = Development ? Development->FindProgression(Row.ContenderId) : nullptr;
        UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_SUMMARY_ROW generation=%u contender=%s name=%s kind=%s left=%d winner=%d level=%u xp=%lld kills=%llu deaths=%llu assists=%llu choices=%d %s"),
            View.Generation, *LDLog::Id(Row.ContenderId), *LDLog::Quote(Row.Name), Row.Kind == ELDContenderKind::Human ? TEXT("Human") : TEXT("AI"),
            Row.bLeft ? 1 : 0, Row.ContenderId == View.Winner ? 1 : 0, uint32(Row.FinalLevel), Progression ? Progression->CumulativeXP : int64(-1),
            Row.Kills, Row.Deaths, Row.Assists,
            Progression ? int32(Progression->ActiveConsumed) : -1, Build ? *BuildText(*Build) : TEXT("skills=- blueprints=- points=-"));
    }
}
