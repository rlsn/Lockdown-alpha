#include "Tests/LDDemoOpponentProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentController.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDCaptureReducer.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Inventory/LDInventoryDirector.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// DEV-only telemetry: one idle Listen Host and three opponent AI play several matches back to back through the
// normal lobby. The Host only reads authority state to record how each match unfolded; it never enters play or
// feeds anything back to a policy. This records numbers for review, it is not an acceptance check of the AI.
enum class ELDOpponentTelemetryStage : uint8 { Join, Lobby, Starting, Match, Results };

namespace LDDemoOpponentTelemetryDetail
{
constexpr uint8 OpponentCount = 3;
constexpr int64 CurveMs = 1000;
constexpr int32 MaxMatches = 50;
constexpr uint32 MatchSchema = 1;

const TCHAR* KindName(ELDContenderKind Kind) { return Kind == ELDContenderKind::OpponentAI ? TEXT("OpponentAI") : TEXT("Human"); }

const TCHAR* ReasonName(ELDMatchEndReason Reason)
{
    switch (Reason)
    {
    case ELDMatchEndReason::GuardSucceeded: return TEXT("GuardSucceeded");
    case ELDMatchEndReason::HostEnded: return TEXT("HostEnded");
    default: return TEXT("None");
    }
}

const TCHAR* CauseName(ELDKillCause Cause)
{
    switch (Cause)
    {
    case ELDKillCause::Contender: return TEXT("Contender");
    case ELDKillCause::CampLow: return TEXT("CampLow");
    case ELDKillCause::CampHigh: return TEXT("CampHigh");
    default: return TEXT("Unowned");
    }
}

FString Id(const FGuid& Value) { return Value.IsValid() ? Value.ToString(EGuidFormats::Digits) : FString(); }

TArray<TSharedPtr<FJsonValue>> Numbers(const TArray<int64>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    Out.Reserve(Values.Num());
    for (const int64 Value : Values) { Out.Add(MakeShared<FJsonValueNumber>(double(Value))); }
    return Out;
}
}

// One contender of the locked roster, read only on the Host.
struct FLDTelemetryContender
{
    FGuid Contender;
    FString Name;
    ELDContenderKind Kind = ELDContenderKind::Human;
    int32 Slot = INDEX_NONE;
    TArray<int64> CurveMs, Level, XP, Gold;
    // Instances currently held; a new instance is a purchase and a vanished one a sale.
    TMap<FGuid, FName> Held;
    TArray<TSharedPtr<FJsonValue>> Purchases, Sales;
};

class FLDDemoOpponentTelemetry : public TSharedFromThis<FLDDemoOpponentTelemetry>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Error;
    int32 Matches = 1, Completed = 0, TimeoutSeconds = 720;
    double Deadline = 0, LastCommandAt = -100, ReturnedAt = 0;
    ELDOpponentTelemetryStage Stage = ELDOpponentTelemetryStage::Join;
    bool StartSent = false, ReturnSent = false;
    uint32 MatchGeneration = 0;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Files;

    // Per match.
    int64 StartedAtMs = -1, NextCurveMs = 0, OpenedAtMs = -1, FirstKillMs = -1, FirstDeathMs = -1;
    uint64 LastKillSerial = 0;
    uint32 KillFeedGaps = 0;
    TArray<TSharedPtr<FJsonValue>> Kills;
    TArray<FLDTelemetryContender> Rows;
    FLDCaptureDevTally Capture;

    bool Resend()
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - LastCommandAt < 3.) { return false; }
        LastCommandAt = Now;
        return true;
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Fail(const FString& Detail)
    {
        if (Error.IsEmpty()) { Error = Detail; }
        return Finish();
    }

    bool Finish()
    {
        const bool bPassed = Error.IsEmpty() && Completed == Matches;
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), bPassed);
        Report->SetStringField(TEXT("role"), TEXT("Telemetry"));
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("matches_requested"), Matches);
        Report->SetNumberField(TEXT("matches_completed"), Completed);
        Report->SetArrayField(TEXT("files"), Files);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only telemetry: one idle Listen Host and three opponent AI through the normal lobby, several matches in a row. Records how the matches went; it does not judge the AI or the balance."));
        const bool bWritten = Save(Report, TEXT("Telemetry.json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_OPPONENT_TELEMETRY passed=%d matches=%d/%d detail=%s"), bPassed && bWritten,
            Completed, Matches, *Error);
        FPlatformMisc::RequestExitWithStatus(false, bPassed && bWritten ? 0 : 2);
        return false;
    }

    bool BeginMatch(const FLDLobbyView& Lobby, const FLDPublicMatchView& View)
    {
        using namespace LDDemoOpponentTelemetryDetail;
        // Under DEV content hot reload each match pins the content synced before it started; read that one.
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content)
        {
            return Fail(TEXT("This match's content does not load: ") + Error);
        }
        StartedAtMs = View.StartedAtMs;
        NextCurveMs = 0;
        OpenedAtMs = FirstKillMs = FirstDeathMs = -1;
        LastKillSerial = 0;
        KillFeedGaps = 0;
        Kills.Reset();
        Rows.Reset();
        Capture = FLDCaptureDevTally();
        int32 Slot = 0;
        for (const FLDLobbyMember& Member : Lobby.Members)
        {
            FLDTelemetryContender& Row = Rows.AddDefaulted_GetRef();
            Row.Contender = Member.ContenderId;
            Row.Name = Member.Name;
            Row.Kind = Member.Kind;
            Row.Slot = Member.Kind == ELDContenderKind::OpponentAI ? Slot++ : INDEX_NONE;
        }
        return true;
    }

    void Sample(ALDGameMode& Mode, const ALDGameState& Public)
    {
        using namespace LDDemoOpponentTelemetryDetail;
        const FLDPublicMatchView& View = Public.PublicMatch;
        const int64 Now = Mode.GetAuthorityNowMs();
        if (OpenedAtMs < 0 && View.CircleState != ELDCircleState::NotOpen) { OpenedAtMs = View.OpenAtMs - StartedAtMs; }
        Capture = Mode.DevCaptureTally();
        // The feed is a bounded ring; a serial jump between two reads means notices were missed.
        for (const FLDKillNotice& Notice : Public.KillFeed.Notices)
        {
            if (Notice.Serial <= LastKillSerial) { continue; }
            KillFeedGaps += Notice.Serial > LastKillSerial + 1 ? 1 : 0;
            LastKillSerial = Notice.Serial;
            const int64 At = Notice.OccurredAtMs - StartedAtMs;
            if (FirstDeathMs < 0) { FirstDeathMs = At; }
            if (FirstKillMs < 0 && Notice.Cause == ELDKillCause::Contender) { FirstKillMs = At; }
            auto Kill = MakeShared<FJsonObject>();
            Kill->SetNumberField(TEXT("t_ms"), double(At));
            Kill->SetStringField(TEXT("victim"), Id(Notice.Victim));
            Kill->SetStringField(TEXT("killer"), Id(Notice.Killer));
            Kill->SetStringField(TEXT("cause"), CauseName(Notice.Cause));
            Kills.Add(MakeShared<FJsonValueObject>(Kill));
        }
        const bool bCurve = Now - StartedAtMs >= NextCurveMs;
        if (bCurve) { NextCurveMs = (Now - StartedAtMs) / CurveMs * CurveMs + CurveMs; }
        for (FLDTelemetryContender& Row : Rows)
        {
            const FLDEquipmentView Equipment = Mode.Inventory ? Mode.Inventory->ReadEquipment(Row.Contender) : FLDEquipmentView();
            if (Equipment.bValid) { TrackItems(Row, Equipment, Now - StartedAtMs); }
            if (!bCurve) { continue; }
            const FLDProgressionView Progression = Mode.Development ? Mode.Development->ReadProgression(Row.Contender)
                : FLDProgressionView();
            Row.CurveMs.Add(Now - StartedAtMs);
            Row.Level.Add(Progression.bValid ? Progression.Level : -1);
            Row.XP.Add(Progression.bValid ? Progression.CumulativeXP : -1);
            Row.Gold.Add(Equipment.bValid ? Equipment.Gold : -1);
        }
    }

    void TrackItems(FLDTelemetryContender& Row, const FLDEquipmentView& Equipment, int64 At)
    {
        TMap<FGuid, FName> Now;
        for (const FLDEquipmentSlotView& Slot : Equipment.Slots)
        {
            if (!Slot.bOccupied) { continue; }
            Now.Add(Slot.Instance, Slot.DefinitionId);
            if (Row.Held.Contains(Slot.Instance)) { continue; }
            const FLDDemoEquipmentDefinition* Definition = nullptr;
            FString LookupError;
            auto Purchase = MakeShared<FJsonObject>();
            Purchase->SetNumberField(TEXT("t_ms"), double(At));
            Purchase->SetStringField(TEXT("item"), Slot.DefinitionId.ToString());
            Purchase->SetNumberField(TEXT("slot"), Slot.SlotIndex);
            Purchase->SetNumberField(TEXT("price"), Content && Content->FindEquipment(Slot.DefinitionId.ToString(), Definition, LookupError)
                && Definition ? double(Definition->BuyPrice) : -1.);
            Purchase->SetNumberField(TEXT("gold_after"), double(Equipment.Gold));
            Row.Purchases.Add(MakeShared<FJsonValueObject>(Purchase));
        }
        for (const TPair<FGuid, FName>& Pair : Row.Held)
        {
            if (Now.Contains(Pair.Key)) { continue; }
            auto Sale = MakeShared<FJsonObject>();
            Sale->SetNumberField(TEXT("t_ms"), double(At));
            Sale->SetStringField(TEXT("item"), Pair.Value.ToString());
            Sale->SetNumberField(TEXT("gold_after"), double(Equipment.Gold));
            Row.Sales.Add(MakeShared<FJsonValueObject>(Sale));
        }
        Row.Held = MoveTemp(Now);
    }

    bool WriteMatch(const FLDResultView& Result)
    {
        using namespace LDDemoOpponentTelemetryDetail;
        auto Match = MakeShared<FJsonObject>();
        Match->SetNumberField(TEXT("schema"), MatchSchema);
        Match->SetNumberField(TEXT("match"), Completed + 1);
        Match->SetNumberField(TEXT("generation"), Result.Generation);
        Match->SetStringField(TEXT("result_id"), Id(Result.ResultId));
        // The catalog root of the content this match played; under hot reload it changes with every re-sync.
        Match->SetStringField(TEXT("content_root"), Content ? Content->Get().Identity.Root.ToHex() : FString());
        Match->SetBoolField(TEXT("dev_content_reload"), LDDemoEntry::IsDevContentReload());
        Match->SetStringField(TEXT("reason"), ReasonName(Result.Reason));
        Match->SetStringField(TEXT("winner"), Id(Result.Winner));
        Match->SetNumberField(TEXT("duration_ms"), double(Result.DurationMs));
        Match->SetNumberField(TEXT("capture_open_ms"), double(OpenedAtMs));
        Match->SetNumberField(TEXT("first_kill_ms"), double(FirstKillMs));
        Match->SetNumberField(TEXT("first_death_ms"), double(FirstDeathMs));
        Match->SetNumberField(TEXT("kill_feed_gaps"), KillFeedGaps);
        Match->SetArrayField(TEXT("kills"), Kills);
        Match->SetNumberField(TEXT("capture_contests"), Capture.Contests);
        TArray<TSharedPtr<FJsonValue>> Contenders;
        for (const FLDTelemetryContender& Row : Rows)
        {
            const FLDResultRow* Final = Result.Rows.FindByPredicate([&Row](const FLDResultRow& R) { return R.ContenderId == Row.Contender; });
            if (!Final) { return Fail(TEXT("The frozen result does not list a contender of the locked roster: ") + Row.Name); }
            const FLDCaptureDevTally::FHolder* Hold = Capture.Holders.Find(Row.Contender);
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("contender"), Id(Row.Contender));
            Value->SetStringField(TEXT("name"), Row.Name);
            Value->SetStringField(TEXT("kind"), KindName(Row.Kind));
            Value->SetNumberField(TEXT("slot"), Row.Slot);
            Value->SetBoolField(TEXT("winner"), Row.Contender == Result.Winner);
            Value->SetBoolField(TEXT("left"), Final->bLeft);
            Value->SetNumberField(TEXT("final_level"), Final->FinalLevel);
            Value->SetNumberField(TEXT("kills"), double(Final->Kills));
            Value->SetNumberField(TEXT("deaths"), double(Final->Deaths));
            Value->SetNumberField(TEXT("capture_attempts"), Hold ? Hold->Attempts : 0);
            Value->SetNumberField(TEXT("capture_resumes"), Hold ? Hold->Resumes : 0);
            Value->SetNumberField(TEXT("capture_paused"), Hold ? Hold->Paused : 0);
            Value->SetNumberField(TEXT("capture_lost"), Hold ? Hold->Lost : 0);
            Value->SetArrayField(TEXT("purchases"), Row.Purchases);
            Value->SetArrayField(TEXT("sales"), Row.Sales);
            auto Curve = MakeShared<FJsonObject>();
            Curve->SetArrayField(TEXT("t_ms"), Numbers(Row.CurveMs));
            Curve->SetArrayField(TEXT("level"), Numbers(Row.Level));
            Curve->SetArrayField(TEXT("xp"), Numbers(Row.XP));
            Curve->SetArrayField(TEXT("gold"), Numbers(Row.Gold));
            Value->SetObjectField(TEXT("curve"), Curve);
            Contenders.Add(MakeShared<FJsonValueObject>(Value));
        }
        Match->SetArrayField(TEXT("contenders"), Contenders);
        const FString Name = FString::Printf(TEXT("Match%02d.json"), Completed + 1);
        if (!Save(Match, Name)) { return Fail(TEXT("Could not persist telemetry file ") + Name); }
        Files.Add(MakeShared<FJsonValueString>(Name));
        ++Completed;
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_OPPONENT_TELEMETRY match=%d reason=%s duration_ms=%lld"), Completed,
            ReasonName(Result.Reason), Result.DurationMs);
        return true;
    }

    bool Tick();
};

bool FLDDemoOpponentTelemetry::Tick()
{
    using namespace LDDemoOpponentTelemetryDetail;
    if (FPlatformTime::Seconds() >= Deadline)
    {
        return Fail(FString::Printf(TEXT("Telemetry match %d timed out at stage %u."), Completed + 1, uint8(Stage)));
    }
    auto* PC = Controller.Get();
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!PC || !World || !Public || !Mode) { return true; }
    if (!Content && (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content))
    {
        return Fail(TEXT("Approved content does not load: ") + Error);
    }
    const FLDLobbyView& Lobby = Public->DemoLobby;
    const FLDPublicMatchView& View = Public->PublicMatch;
    switch (Stage)
    {
    case ELDOpponentTelemetryStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        if (World->GetNetMode() != NM_ListenServer) { return Fail(TEXT("Telemetry runs on a Listen Host.")); }
        Stage = ELDOpponentTelemetryStage::Lobby;
        return true;
    case ELDOpponentTelemetryStage::Lobby:
    {
        if (Lobby.Phase != ELDMatchPhase::Lobby) { return true; }
        int32 Humans = 0;
        for (const FLDLobbyMember& Row : Lobby.Members) { Humans += Row.Kind == ELDContenderKind::Human ? 1 : 0; }
        if (Humans != 1) { return Fail(TEXT("Telemetry needs the Host as the only human in the room.")); }
        if (Lobby.AICount != OpponentCount)
        {
            if (Resend()) { PC->SetDemoAICount(OpponentCount); }
            return true;
        }
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC->OwnerState.ContenderId; });
        if (!Self || !Self->Ready)
        {
            if (Resend()) { PC->SetDemoReady(true); }
            return true;
        }
        if (StartSent) { return true; }
        if (!Lobby.CanStart || !Lobby.StartBlocker.IsEmpty())
        {
            return Fail(TEXT("The ready room cannot start: ") + Lobby.StartBlocker);
        }
        MatchGeneration = Lobby.Generation;
        PC->StartDemoMatch();
        StartSent = true;
        Stage = ELDOpponentTelemetryStage::Starting;
        return true;
    }
    case ELDOpponentTelemetryStage::Starting:
        if (Lobby.Phase == ELDMatchPhase::Lobby && Lobby.Generation != MatchGeneration)
        {
            return Fail(TEXT("The start aborted: ") + PC->IntentFeedback);
        }
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation) { return true; }
        if (!BeginMatch(Lobby, View)) { return false; }
        Stage = ELDOpponentTelemetryStage::Match;
        return true;
    case ELDOpponentTelemetryStage::Match:
    {
        if (View.Phase == ELDMatchPhase::Development || View.Phase == ELDMatchPhase::CaptureOpen) { Sample(*Mode, *Public); }
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        const FLDResultView& Result = Public->MatchResult;
        if (Result.Generation != View.Generation || Result.ResultId != View.ResultId) { return true; }
        // The closing step may have changed gold, items or the capture tally after the last sample.
        Capture = Mode->DevCaptureTally();
        for (FLDTelemetryContender& Row : Rows)
        {
            const FLDEquipmentView Equipment = Mode->Inventory ? Mode->Inventory->ReadEquipment(Row.Contender) : FLDEquipmentView();
            if (Equipment.bValid) { TrackItems(Row, Equipment, Result.EndedAtMs - StartedAtMs); }
        }
        if (!WriteMatch(Result)) { return false; }
        ReturnSent = false;
        ReturnedAt = 0;
        Stage = ELDOpponentTelemetryStage::Results;
        return true;
    }
    case ELDOpponentTelemetryStage::Results:
    {
        if (!ReturnSent)
        {
            PC->ReturnDemoToLobby();
            ReturnSent = true;
        }
        if (Lobby.Generation == MatchGeneration || Lobby.Phase != ELDMatchPhase::Lobby || View.Generation != Lobby.Generation
            || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
        // Each adapter ends itself on its first tick after the core retired its match; the next match waits for that.
        int32 Remaining = 0;
        for (TActorIterator<ALDOpponentController> It(World); It; ++It) { Remaining += IsValid(*It) ? 1 : 0; }
        if (!ReturnedAt) { ReturnedAt = FPlatformTime::Seconds(); }
        if (Remaining)
        {
            if (FPlatformTime::Seconds() - ReturnedAt < 15.) { return true; }
            return Fail(TEXT("An AI controller outlived its match."));
        }
        if (Completed >= Matches) { return Finish(); }
        StartSent = false;
        Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        Stage = ELDOpponentTelemetryStage::Lobby;
        return true;
    }
    default:
        return true;
    }
}

void LDDemoTests::StartOpponentTelemetry(ALDPlayerController& Controller, const FString& Output, int32 TimeoutSeconds)
{
    using namespace LDDemoOpponentTelemetryDetail;
    auto Telemetry = MakeShared<FLDDemoOpponentTelemetry>();
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentMatches="), Telemetry->Matches)
        || Telemetry->Matches < 1 || Telemetry->Matches > MaxMatches)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_OPPONENT_TELEMETRY Invalid match count."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Telemetry->Controller = &Controller;
    Telemetry->Output = Output;
    Telemetry->TimeoutSeconds = TimeoutSeconds;
    // Each match, lobby to the next lobby, gets its own bounded deadline.
    Telemetry->Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Telemetry](float) { return Telemetry->Tick(); }), .1f);
}
#endif
