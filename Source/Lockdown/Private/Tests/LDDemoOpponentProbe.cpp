#include "Tests/LDDemoOpponentProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentController.h"
#include "Character/LDCharacter.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Inventory/LDInventoryDirector.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Vision/LDWorldViewRelay.h"

// Three humans ready a full room with three opponent AI and then stay out of play, so every growth step, trade,
// fight and the capture win below come from the AI's own requests through the shared domain entries.
enum class ELDOpponentProbeStage : uint8 { Join, Lobby, Starting, Match, Results, Done };
// Only a check the match actually exercised can pass. One the match gave no occasion for, such as respawning when no AI
// died, is reported apart as not exercised and never counted as passed.
enum class ELDOpponentProbeCheck : uint8 { Passed, Failed, NotExercised };

namespace LDDemoOpponentProbeDetail
{
constexpr uint8 OpponentCount = 3;
constexpr int32 HumanCount = 3;
constexpr int64 SampleMs = 1000;
// Bounds of this check, not game values. A loop at the approved decision rate sends 120 body requests in 30 s;
// fighting with every source and target change stays well below 40.
constexpr int64 RequestWindowMs = 30000;
constexpr uint32 MaxBodyRequestsPerWindow = 40;
constexpr int64 RespawnSlackMs = 2500;
constexpr int64 ActAfterRespawnMs = 10000;
constexpr int64 OpeningPickWithinMs = 10000;
constexpr double MinTravelCm = 1000.;

int32 CountKind(const FLDLobbyView& Lobby, ELDContenderKind Kind)
{
    int32 Count = 0;
    for (const FLDLobbyMember& Row : Lobby.Members) { Count += Row.Kind == Kind ? 1 : 0; }
    return Count;
}

TSet<FGuid> OpponentRows(const FLDLobbyView& Lobby)
{
    TSet<FGuid> Ids;
    for (const FLDLobbyMember& Row : Lobby.Members)
    {
        if (Row.Kind == ELDContenderKind::OpponentAI) { Ids.Add(Row.ContenderId); }
    }
    return Ids;
}

int32 OpponentSlot(const FLDLobbyView& Lobby, const FGuid& Contender)
{
    int32 Slot = 0;
    for (const FLDLobbyMember& Row : Lobby.Members)
    {
        if (Row.Kind != ELDContenderKind::OpponentAI) { continue; }
        if (Row.ContenderId == Contender) { return Slot; }
        ++Slot;
    }
    return INDEX_NONE;
}
}

// Authority truth about one AI, read only by this checker on the Host and never fed back to any policy.
struct FLDOpponentProbeTrack
{
    FGuid Contender;
    int32 Slot = INDEX_NONE;
    TWeakObjectPtr<ALDOpponentController> Controller;
    ELDLifePhase Phase = ELDLifePhase::Unspawned;
    uint32 Life = 0;
    int64 DeadSeenAtMs = -1;
    uint32 Deaths = 0;
    uint32 Respawns = 0;
    int64 LongestRespawnMs = 0;
    bool bRespawnsValid = true;
    int64 ActCheckDueMs = -1;
    // Body requests counted at the first sample that saw this AI dead. A dead body sends nothing, so any later request
    // belongs to the next life, including one sent in its very first frame.
    uint32 ActBase = 0;
    bool bQuietWhileDead = true;
    uint32 RespawnsChecked = 0;
    uint32 RespawnsActed = 0;
    double TravelCm = 0;
    FVector LastFeet = FVector::ZeroVector;
    bool bHasFeet = false;
    int64 MaxLevel = 0;
    int64 MaxXP = 0;
    int64 MaxConsumed = 0;
    // Match ms at the first sample that saw a consumed choice; the D10 opening pick resolves right after spawn.
    int64 OpeningPickMs = -1;
    int64 MaxGold = 0;
    int32 MaxItems = 0;
    FString Gear;
    TArray<TPair<int64, uint32>> Requests;
    uint32 MaxWindowRequests = 0;
    FLDOpponentRequestTally Tally;
};

class FLDDemoOpponentProbe : public TSharedFromThis<FLDDemoOpponentProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, LastCommandAt = -100, ReturnedAt = 0;
    ELDOpponentProbeStage Stage = ELDOpponentProbeStage::Join;
    bool Passed = true, Written = false, StartSent = false, ReturnSent = false;
    bool RosterKept = true, ReadsKept = true;
    uint32 FirstGeneration = 0;
    int64 RespawnMs = 0, NextSampleMs = 0, StartedAtMs = -1;
    int32 MaxSeenOpponents = 0;
    // Rendered Host only: up to three presentation screenshots of the running match (HUD review evidence, never a check).
    int32 Shots = 0;
    double MatchSeenAt = 0, LastShotAt = 0;
    TArray<TSharedPtr<FJsonValue>> Screenshots;
    void CaptureHost(const ALDPlayerController& Owner);
    TSet<FGuid> OpponentIds;
    TArray<FLDOpponentProbeTrack> Tracks;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<TSharedPtr<FJsonValue>> NotExercised;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    bool Check(bool Value, const TCHAR* Name)
    {
        Record(Value ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed, Name);
        return Value;
    }

    void Record(ELDOpponentProbeCheck Result, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Result == ELDOpponentProbeCheck::Passed);
        Row->SetStringField(TEXT("status"), Result == ELDOpponentProbeCheck::Passed ? TEXT("passed")
            : Result == ELDOpponentProbeCheck::Failed ? TEXT("failed") : TEXT("not_exercised"));
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (Result == ELDOpponentProbeCheck::NotExercised)
        {
            NotExercised.Add(MakeShared<FJsonValueString>(Name));
            return;
        }
        if (Result == ELDOpponentProbeCheck::Failed)
        {
            Passed = false;
            if (Error.IsEmpty()) { Error = Name; }
        }
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Error = TEXT("Could not persist opponent probe marker: ") + Name;
        Passed = false;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output, FString(Name) + Suffix))) { return false; }
        }
        return true;
    }

    // Session commands are idempotent per roster; a refused or raced one is sent again after a pause.
    bool Resend()
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - LastCommandAt < 3.) { return false; }
        LastCommandAt = Now;
        return true;
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only full room: participating Listen Host, two owning clients and three opponent AI through the normal lobby; the humans stay out of play. Not hidden-state non-interference, field whitelists, a leaving human or network faults."));
        Summary->SetArrayField(TEXT("not_exercised"), NotExercised);
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_OPPONENT_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    bool Tick();
    bool BindOpponents(UWorld& World, ALDGameMode& Mode, const FLDLobbyView& Lobby);
    void Sample(UWorld& World, ALDGameMode& Mode);
    void ReadGrowth(ALDGameMode& Mode, FLDOpponentProbeTrack& Track) const;
    void CloseHost(ALDGameMode& Mode, int64 EndedAtMs, const FLDResultView& Result);
};

bool FLDDemoOpponentProbe::BindOpponents(UWorld& World, ALDGameMode& Mode, const FLDLobbyView& Lobby)
{
    using namespace LDDemoOpponentProbeDetail;
    TSet<FGuid> Bound;
    bool ServerOnly = true, Bodies = true;
    for (TActorIterator<ALDOpponentController> It(&World); It; ++It)
    {
        ALDOpponentController* AI = *It;
        if (!IsValid(AI)) { continue; }
        const FGuid Id = Mode.ContenderId(AI);
        ServerOnly &= AI->HasAuthority() && !AI->GetNetConnection() && !AI->Player && Id.IsValid() && !Bound.Contains(Id);
        Bound.Add(Id);
        FLDContenderLifeRead Life;
        FString ReadError;
        Bodies &= Cast<ALDCharacter>(AI->GetPawn()) && Mode.ReadContenderLife(Mode.GetMatchContext(), Id, Life, ReadError)
            && Life.Phase == ELDLifePhase::Alive;
        FLDOpponentProbeTrack& Track = Tracks.AddDefaulted_GetRef();
        Track.Contender = Id;
        Track.Slot = OpponentSlot(Lobby, Id);
        Track.Controller = AI;
        Track.Phase = Life.Phase;
        Track.Life = Life.Life;
    }
    Check(ServerOnly && Bound.Num() == OpponentCount && Bound.Includes(OpponentIds) && OpponentIds.Includes(Bound),
        TEXT("Each AI slot is one server-only opponent controller bound to its own lobby contender"));
    Check(Bodies, TEXT("Each AI enters the match with a live body, like a human"));
    return Passed;
}

void FLDDemoOpponentProbe::ReadGrowth(ALDGameMode& Mode, FLDOpponentProbeTrack& Track) const
{
    if (Mode.Development)
    {
        const FLDProgressionView Progression = Mode.Development->ReadProgression(Track.Contender);
        if (Progression.bValid)
        {
            Track.MaxLevel = FMath::Max<int64>(Track.MaxLevel, Progression.Level);
            Track.MaxXP = FMath::Max<int64>(Track.MaxXP, Progression.CumulativeXP);
            Track.MaxConsumed = FMath::Max<int64>(Track.MaxConsumed, Progression.ConsumedThrough);
        }
    }
    if (Mode.Inventory)
    {
        const FLDEquipmentView Equipment = Mode.Inventory->ReadEquipment(Track.Contender);
        if (Equipment.bValid)
        {
            int32 Items = 0;
            FString Gear;
            for (const FLDEquipmentSlotView& Slot : Equipment.Slots)
            {
                if (!Slot.bOccupied) { continue; }
                ++Items;
                Gear += (Gear.IsEmpty() ? TEXT("") : TEXT(",")) + Slot.DefinitionId.ToString();
            }
            Track.MaxGold = FMath::Max<int64>(Track.MaxGold, Equipment.Gold);
            Track.MaxItems = FMath::Max(Track.MaxItems, Items);
            Track.Gear = Gear;
        }
    }
}

void FLDDemoOpponentProbe::Sample(UWorld& World, ALDGameMode& Mode)
{
    using namespace LDDemoOpponentProbeDetail;
    const int64 Now = Mode.GetAuthorityNowMs();
    if (Now < NextSampleMs) { return; }
    NextSampleMs = Now + SampleMs;
    if (StartedAtMs < 0) { StartedAtMs = Mode.ReadPublicMatch().StartedAtMs; }
    // No backfill or replacement: the same three adapters for the same three contenders throughout.
    TSet<FGuid> Live;
    for (TActorIterator<ALDOpponentController> It(&World); It; ++It)
    {
        if (IsValid(*It)) { Live.Add(Mode.ContenderId(*It)); }
    }
    RosterKept &= Live.Num() == OpponentCount && Live.Includes(OpponentIds);
    for (FLDOpponentProbeTrack& Track : Tracks)
    {
        ALDOpponentController* AI = Track.Controller.Get();
        FLDContenderLifeRead Life;
        FString ReadError;
        if (!AI || !Mode.ReadContenderLife(Mode.GetMatchContext(), Track.Contender, Life, ReadError))
        {
            ReadsKept = false;
            continue;
        }
        Track.Tally = AI->GetRequestTally();
        const uint32 Body = Track.Tally.BodyRequests();
        if (Life.Phase == ELDLifePhase::Dead)
        {
            if (Track.DeadSeenAtMs < 0)
            {
                Track.DeadSeenAtMs = Now;
                ++Track.Deaths;
                Track.ActBase = Body;
            }
            Track.bQuietWhileDead &= Body == Track.ActBase;
        }
        if (Life.Phase == ELDLifePhase::Alive && Track.DeadSeenAtMs >= 0)
        {
            const int64 Waited = Now - Track.DeadSeenAtMs;
            Track.LongestRespawnMs = FMath::Max(Track.LongestRespawnMs, Waited);
            Track.bRespawnsValid &= Life.Life == Track.Life + 1 && Waited <= RespawnMs + RespawnSlackMs;
            ++Track.Respawns;
            Track.DeadSeenAtMs = -1;
            Track.ActCheckDueMs = Now + ActAfterRespawnMs;
            Track.bHasFeet = false;
        }
        if (Track.ActCheckDueMs >= 0 && (Body > Track.ActBase || Now >= Track.ActCheckDueMs))
        {
            ++Track.RespawnsChecked;
            Track.RespawnsActed += Body > Track.ActBase ? 1 : 0;
            Track.ActCheckDueMs = -1;
        }
        Track.Phase = Life.Phase;
        Track.Life = Life.Life;
        const APawn* Pawn = Life.Phase == ELDLifePhase::Alive ? AI->GetPawn() : nullptr;
        if (Pawn)
        {
            const FVector Feet = Pawn->GetActorLocation();
            if (Track.bHasFeet) { Track.TravelCm += FVector::Dist2D(Track.LastFeet, Feet); }
            Track.LastFeet = Feet;
            Track.bHasFeet = true;
        }
        else { Track.bHasFeet = false; }
        ReadGrowth(Mode, Track);
        if (Track.OpeningPickMs < 0 && Track.MaxConsumed >= 1) { Track.OpeningPickMs = Now - StartedAtMs; }
        Track.Requests.Add(TPair<int64, uint32>(Now, Body));
        while (Track.Requests.Num() > 1 && Track.Requests[0].Key < Now - RequestWindowMs) { Track.Requests.RemoveAt(0); }
        Track.MaxWindowRequests = FMath::Max(Track.MaxWindowRequests, Body - Track.Requests[0].Value);
    }
}

void FLDDemoOpponentProbe::CaptureHost(const ALDPlayerController& Owner)
{
    // Only a rendered Host takes them: the opening HUD (own bars, pending three-choice, minimap), then the first two
    // moments an AI stands in the Host's current sight, where cooldowns, floating numbers and falls can show.
    if (!FApp::CanEverRender() || Shots >= 3) { return; }
    const double Now = FPlatformTime::Seconds();
    if (MatchSeenAt == 0) { MatchSeenAt = Now; }
    int32 Visible = 0;
    if (const ALDWorldViewRelay* Relay = Owner.LocalView())
    {
        // Only an opponent AI in the Host's current sight counts, and after the opening only while it acts (an attack,
        // cast or hit cue), so the shots catch fights rather than the other humans standing nearby.
        const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
        if (Read.Frame && Read.Status == ELDObservedViewStatus::Current)
        {
            for (const FLDObservedUnitView& Unit : Read.Frame->Units)
            {
                Visible += Unit.Kind == ELDObservedUnitKind::Contender && OpponentIds.Contains(Unit.ContenderId)
                    && Unit.ActionCue != ELDObservedActionCue::None ? 1 : 0;
            }
        }
    }
    const bool bOpening = Shots == 0 && Now - MatchSeenAt >= 8.;
    const bool bSighted = Shots > 0 && Visible > 0 && Now - LastShotAt >= 6.;
    if (!bOpening && !bSighted) { return; }
    const FString File = FString::Printf(TEXT("HostMatch%d.png"), ++Shots);
    LastShotAt = Now;
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, File), true, false);
    Screenshots.Add(MakeShared<FJsonValueString>(File));
}

void FLDDemoOpponentProbe::CloseHost(ALDGameMode& Mode, int64 EndedAtMs, const FLDResultView& Result)
{
    using namespace LDDemoOpponentProbeDetail;
    Check(RosterKept && ReadsKept && Tracks.Num() == OpponentCount, TEXT("No AI is added, replaced or lost during the match"));
    bool Moved = true, WinnerGrew = false, Bought = false, RespawnsOnTime = true, Acted = true, Bounded = true, QuietWhileDead = true;
    bool OpeningPicks = true;
    uint32 Respawns = 0, RespawnsChecked = 0, DeathsSeen = 0;
    uint32 PausedHolds = 0, PausedHoldsKept = 0, PausedHoldsBroken = 0, Challenges = 0, ChallengesKept = 0, ChallengesBroken = 0;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (FLDOpponentProbeTrack& Track : Tracks)
    {
        if (const ALDOpponentController* AI = Track.Controller.Get()) { Track.Tally = AI->GetRequestTally(); }
        ReadGrowth(Mode, Track);
        // A death whose respawn time had not come when the match closed is not a late respawn.
        if (Track.DeadSeenAtMs >= 0 && EndedAtMs - Track.DeadSeenAtMs > RespawnMs + RespawnSlackMs) { Track.bRespawnsValid = false; }
        // A respawn shortly before the close settles against the final tally once it acted or its window had passed.
        if (Track.ActCheckDueMs >= 0)
        {
            const bool bActed = Track.Tally.BodyRequests() > Track.ActBase;
            if (bActed || EndedAtMs >= Track.ActCheckDueMs)
            {
                ++Track.RespawnsChecked;
                Track.RespawnsActed += bActed ? 1 : 0;
            }
            Track.ActCheckDueMs = -1;
        }
        const FLDOpponentRequestTally& T = Track.Tally;
        Moved &= Track.TravelCm >= MinTravelCm;
        // An AI that loses an even opening fight may spend the match recovering; the winner shows the full growth loop.
        if (Track.Contender == Result.Winner) { WinnerGrew = Track.MaxLevel >= 2 && Track.MaxConsumed >= 1; }
        Bought |= Track.MaxItems > 0;
        RespawnsOnTime &= Track.bRespawnsValid;
        Respawns += Track.Respawns;
        RespawnsChecked += Track.RespawnsChecked;
        Acted &= Track.RespawnsActed == Track.RespawnsChecked;
        DeathsSeen += Track.Deaths;
        QuietWhileDead &= Track.bQuietWhileDead;
        Bounded &= Track.MaxWindowRequests <= MaxBodyRequestsPerWindow && T.OrdersRefused <= T.Orders / 10 + 3;
        OpeningPicks &= Track.OpeningPickMs >= 0 && Track.OpeningPickMs <= OpeningPickWithinMs;
        PausedHolds += T.PausedHolds;
        PausedHoldsKept += T.PausedHoldsKept;
        PausedHoldsBroken += T.PausedHolds - T.PausedHoldsKept - T.PausedHoldsReleased;
        Challenges += T.Challenges;
        ChallengesKept += T.ChallengesKept;
        ChallengesBroken += T.Challenges - T.ChallengesKept - T.ChallengesReleased;
        const FLDResultRow* Row = Result.Rows.FindByPredicate([&Track](const FLDResultRow& R) { return R.ContenderId == Track.Contender; });
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("contender"), Track.Contender.ToString(EGuidFormats::Digits));
        Value->SetNumberField(TEXT("slot"), Track.Slot);
        Value->SetBoolField(TEXT("winner"), Track.Contender == Result.Winner);
        Value->SetNumberField(TEXT("level"), double(Track.MaxLevel));
        Value->SetNumberField(TEXT("xp"), double(Track.MaxXP));
        Value->SetNumberField(TEXT("choices"), double(Track.MaxConsumed));
        Value->SetNumberField(TEXT("opening_pick_ms"), double(Track.OpeningPickMs));
        Value->SetNumberField(TEXT("gold_max"), double(Track.MaxGold));
        Value->SetNumberField(TEXT("items_max"), Track.MaxItems);
        Value->SetStringField(TEXT("gear"), Track.Gear);
        Value->SetNumberField(TEXT("kills"), Row ? double(Row->Kills) : -1.);
        Value->SetNumberField(TEXT("deaths"), Track.Deaths);
        Value->SetNumberField(TEXT("respawns"), Track.Respawns);
        Value->SetBoolField(TEXT("quiet_while_dead"), Track.bQuietWhileDead);
        Value->SetNumberField(TEXT("longest_respawn_ms"), double(Track.LongestRespawnMs));
        Value->SetNumberField(TEXT("travel_cm"), FMath::RoundToDouble(Track.TravelCm));
        Value->SetNumberField(TEXT("orders"), T.Orders);
        Value->SetNumberField(TEXT("orders_refused"), T.OrdersRefused);
        Value->SetNumberField(TEXT("stops"), T.Stops);
        Value->SetNumberField(TEXT("actions"), T.Actions);
        Value->SetNumberField(TEXT("actions_refused"), T.ActionsRefused);
        Value->SetNumberField(TEXT("shop_opens"), T.ShopOpens);
        Value->SetNumberField(TEXT("choice_requests"), T.Choices);
        Value->SetNumberField(TEXT("trades"), T.Trades);
        Value->SetNumberField(TEXT("builds_refused"), T.BuildsRefused);
        Value->SetNumberField(TEXT("max_body_requests_30s"), Track.MaxWindowRequests);
        Value->SetNumberField(TEXT("paused_holds"), T.PausedHolds);
        Value->SetNumberField(TEXT("paused_holds_kept"), T.PausedHoldsKept);
        Value->SetNumberField(TEXT("paused_holds_released"), T.PausedHoldsReleased);
        Value->SetNumberField(TEXT("challenges"), T.Challenges);
        Value->SetNumberField(TEXT("challenges_kept"), T.ChallengesKept);
        Value->SetNumberField(TEXT("challenges_released"), T.ChallengesReleased);
        Value->SetNumberField(TEXT("holder_attacks"), T.HolderAttacks);
        Rows.Add(MakeShared<FJsonValueObject>(Value));
    }
    Summary->SetArrayField(TEXT("opponents"), Rows);
    Check(Moved, TEXT("Every AI moved through the map on its own orders"));
    Check(WinnerGrew, TEXT("The winning AI levelled up and confirmed its growth choices"));
    Check(Bought, TEXT("At least one AI bought equipment through the shop"));
    // A late or missing respawn fails even when no respawn completed; a match where none was due only did not exercise it.
    Record(!RespawnsOnTime ? ELDOpponentProbeCheck::Failed
        : Respawns ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("Every AI death led to its next life within the approved respawn delay"));
    Record(!DeathsSeen ? ELDOpponentProbeCheck::NotExercised
        : QuietWhileDead ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("A dead AI sends no body request until its next life"));
    Record(!RespawnsChecked ? ELDOpponentProbeCheck::NotExercised
        : Acted ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("Every respawned AI sent a new body request within ten seconds"));
    Check(Bounded, TEXT("AI requests stay bounded: no request loop and few refusals"));
    Check(OpeningPicks, TEXT("D10: every AI resolved its opening pick within ten seconds of the start"));
    // D12: decisions are counted from each adapter's own frames; a released decision is a stop-loss exit.
    Record(!PausedHolds ? ELDOpponentProbeCheck::NotExercised
        : !PausedHoldsBroken && PausedHoldsKept ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("D12: an AI holding paused progress stays to fight in the circle, leaving only at its stop-loss line"));
    Record(!Challenges ? ELDOpponentProbeCheck::NotExercised
        : !ChallengesBroken && ChallengesKept ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("D12: an AI inside a contest against a rival's paused progress keeps fighting there instead of leaving to farm or shop"));
    Check(!Mode.IsGameplayWriteOpen(Mode.GetMatchContext()) && Mode.ReadPublicMatch().ResultId == Result.ResultId,
        TEXT("The authority gate is closed by the same result it published"));
}

bool FLDDemoOpponentProbe::Tick()
{
    using namespace LDDemoOpponentProbeDetail;
    if (Written)
    {
        if (Role != TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("FinishClients"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        if (Role == TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("Finish"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Check(false, TEXT("Opponent scenario completed within its bounded deadline"));
        Error = FString::Printf(TEXT("Opponent probe timed out at stage %u."), uint8(Stage));
        return Finish();
    }
    auto* PC = Controller.Get();
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!PC || !World || !Public) { return true; }
    if (!Content)
    {
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content || Content->Get().Parameters.RespawnMs <= 0)
        {
            Check(false, TEXT("Approved content with a positive respawn delay loads"));
            return Finish();
        }
        RespawnMs = Content->Get().Parameters.RespawnMs;
    }
    const bool bHost = Role == TEXT("Host");
    if (bHost && !Mode) { return true; }
    const FLDLobbyView& Lobby = Public->DemoLobby;
    const FLDPublicMatchView& View = Public->PublicMatch;
    switch (Stage)
    {
    case ELDOpponentProbeStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        Check(bHost ? World->GetNetMode() == NM_ListenServer
            : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
            TEXT("Each role is a real Listen Host or owning client connection"));
        FirstGeneration = Lobby.Generation;
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDOpponentProbeStage::Lobby;
        return true;
    case ELDOpponentProbeStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || CountKind(Lobby, ELDContenderKind::Human) != HumanCount) { return true; }
        if (Lobby.AICount != OpponentCount)
        {
            // Only the Host selects; everyone readies for the final room, because a change unreadies all humans.
            if (bHost && Resend()) { PC->SetDemoAICount(OpponentCount); }
            return true;
        }
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC->OwnerState.ContenderId; });
        if (!Self || !Self->Ready)
        {
            if (Resend()) { PC->SetDemoReady(true); }
            return true;
        }
        if (!bHost)
        {
            Stage = ELDOpponentProbeStage::Starting;
            return true;
        }
        const bool AllReady = !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
        if (!AllReady || StartSent) { return true; }
        OpponentIds = OpponentRows(Lobby);
        Check(OpponentIds.Num() == OpponentCount && !OpponentIds.Contains(FGuid()),
            TEXT("The Host selects three opponent AI, listed as three distinct AI rows"));
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("Every provider is wired: the ready full room may start normally")))
        {
            Error = TEXT("Normal start is still blocked: ") + Lobby.StartBlocker;
            return Finish();
        }
        if (!Passed) { return Finish(); }
        PC->StartDemoMatch();
        StartSent = true;
        Stage = ELDOpponentProbeStage::Starting;
        return true;
    }
    case ELDOpponentProbeStage::Starting:
    {
        if (bHost && Lobby.Phase == ELDMatchPhase::Lobby && Lobby.Generation != FirstGeneration)
        {
            Check(false, TEXT("The full room starts without aborting"));
            Error = TEXT("The start aborted: ") + PC->IntentFeedback;
            return Finish();
        }
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Cast<ALDCharacter>(PC->GetPawn())
            || PC->OwnerState.LifePhase != ELDLifePhase::Alive) { return true; }
        if (OpponentIds.IsEmpty()) { OpponentIds = OpponentRows(Lobby); }
        Check(CountKind(Lobby, ELDContenderKind::Human) == HumanCount && OpponentIds.Num() == OpponentCount,
            TEXT("The room plays with three humans and three AI on the locked roster"));
        if (bHost && !BindOpponents(*World, *Mode, Lobby)) { return Finish(); }
        if (!Passed) { return Finish(); }
        Stage = ELDOpponentProbeStage::Match;
        return true;
    }
    case ELDOpponentProbeStage::Match:
    {
        if (bHost && (View.Phase == ELDMatchPhase::Development || View.Phase == ELDMatchPhase::CaptureOpen)) { Sample(*World, *Mode); CaptureHost(*PC); }
        if (!bHost)
        {
            // Ordinary vision decides what an owning client sees of the AI; this is evidence, not a requirement.
            int32 Seen = 0;
            if (const ALDWorldViewRelay* Relay = PC->LocalView())
            {
                const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
                if (Read.Frame)
                {
                    for (const FLDObservedUnitView& Unit : Read.Frame->Units)
                    {
                        Seen += Unit.Kind == ELDObservedUnitKind::Contender && OpponentIds.Contains(Unit.ContenderId) ? 1 : 0;
                    }
                }
            }
            MaxSeenOpponents = FMath::Max(MaxSeenOpponents, Seen);
        }
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        const FLDResultView& Result = Public->MatchResult;
        if (Result.Generation != View.Generation || Result.ResultId != View.ResultId) { return true; }
        const FLDResultRow* Winner = Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row) { return Row.ContenderId == Result.Winner; });
        Check(Result.Reason == ELDMatchEndReason::CaptureCompleted && Winner && Winner->Kind == ELDContenderKind::OpponentAI
            && OpponentIds.Contains(Result.Winner), TEXT("An opponent AI wins the match by completing the capture"));
        Check(Result.Rows.Num() == HumanCount + OpponentCount && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row)
            { return Row.FinalLevel < 1 || Row.bLeft; }), TEXT("The frozen result lists all six contenders with committed final levels"));
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("duration_ms"), double(Result.DurationMs));
        if (bHost) { Summary->SetArrayField(TEXT("screenshots"), Screenshots); CloseHost(*Mode, Result.EndedAtMs, Result); }
        else { Summary->SetNumberField(TEXT("max_seen_opponents"), MaxSeenOpponents); }
        if (!Passed || !Mark(Role + TEXT("Results.json"))) { return Finish(); }
        Stage = ELDOpponentProbeStage::Results;
        return true;
    }
    case ELDOpponentProbeStage::Results:
    {
        if (bHost && !ReturnSent && AllMarkers(TEXT("Results.json")))
        {
            PC->ReturnDemoToLobby();
            ReturnSent = true;
        }
        if (Lobby.Generation == FirstGeneration || Lobby.Phase != ELDMatchPhase::Lobby
            || View.Generation != Lobby.Generation || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
        if (bHost)
        {
            // Each adapter ends itself on its first tick after the core retired its match.
            int32 Remaining = 0;
            for (TActorIterator<ALDOpponentController> It(World); It; ++It) { Remaining += IsValid(*It) ? 1 : 0; }
            if (!ReturnedAt) { ReturnedAt = FPlatformTime::Seconds(); }
            if (Remaining && FPlatformTime::Seconds() - ReturnedAt < 15.) { return true; }
            Check(!Remaining, TEXT("Every AI controller ends with its match"));
        }
        Check(CountKind(Lobby, ELDContenderKind::Human) == HumanCount && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
            { return Row.Ready; }), TEXT("The same connections return to a new, unready Lobby generation"));
        Check(View.CircleState == ELDCircleState::NotOpen && !View.ResultId.IsValid(),
            TEXT("The new Lobby carries no capture state or result"));
        Summary->SetNumberField(TEXT("next_generation"), double(Lobby.Generation));
        Summary->SetNumberField(TEXT("next_ai_count"), Lobby.AICount);
        Stage = ELDOpponentProbeStage::Done;
        return Finish();
    }
    default:
        return true;
    }
}

void LDDemoTests::StartOpponentProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoOpponentProbe")) || !Controller.IsLocalController()
        || Cast<ALDOpponentController>(&Controller)
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FLDDemoOpponentProbe>();
    int32 Timeout = 720;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 300 || Timeout > 900
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_OPPONENT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
